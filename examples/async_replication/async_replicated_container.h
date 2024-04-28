#pragma once

#include "../../bricks/sync/waitable_atomic.h"
#include "../../blocks/http/api.h"
#include "../../bricks/time/chrono.h"

#include "../../bricks/distributed/vector_clock.h"

#include <sstream>
#include <queue>

struct ReplicationNode final {
  std::string host;
  uint16_t port;
};

struct ReplicationConfig final {
  std::string host;
  uint16_t port;
  std::vector<ReplicationNode> nodes_list;
  uint32_t delay;
  bool is_verbose;
  bool show_network_errors;
  uint32_t max_waits;
};

struct Uint32Value final {
  uint32_t data;
  void send(current::net::Connection& c) {
    uint32_t to_send = htonl(data);
    c.BlockingWrite(&to_send, sizeof(to_send), true);
  }
  static Uint32Value recv(const std::unique_ptr<current::net::Connection>& c) {
    uint32_t value;
    auto size = c->BlockingRead(reinterpret_cast<uint8_t*>(&value), sizeof(value));
    if (!size) throw;
    value = ntohl(value);
    return Uint32Value{value};
  }
};

template <class CLOCK_T, class VALUE_T>
class AsyncReplicatedContainer {
 private:
  struct Relay final {
    std::string key;
    VALUE_T value;
    std::string replica_id;
    Clocks clock;
  };
  struct SharedState final {
    bool die = false;
    std::map<std::string, VALUE_T> data;
    std::map<std::string, CLOCK_T> clock;
    std::map<std::string, std::queue<Relay> > replication_out;
  };

  std::string sid;
  uint32_t clock_id;
  uint16_t reader_port;
  bool is_ready = false;
  std::vector<ReplicationNode> nodes;
  current::WaitableAtomic<SharedState> state;
  std::vector<std::thread> writers;
  std::thread reader;
  std::thread monitor;
  uint32_t delay;
  bool is_verbose;
  bool show_network_err;
  uint32_t max_waits;

  std::string node_id(std::string host, uint16_t port) {
    std::ostringstream r;
    r << host << ":" << port;
    return r.str();
  }
  std::string node_id(const ReplicationNode& node) { return node_id(node.host, node.port); }

  // Internal method to apply replication or set new value and replicate
  void update(std::pair<std::string, VALUE_T> tuple, bool replicate) {
    if (!is_ready) {
      throw std::logic_error("Replication is not ready");
    }
    state.MutableUse([tuple, replicate, this](SharedState& state) {
      // Add local data
      state.data[tuple.first] = tuple.second;

      // Set clock and send update to replication thread
      if (replicate) {
        if (state.clock.find(tuple.first) == state.clock.end()) {
          state.clock[tuple.first] = CLOCK_T(nodes.size(), clock_id);
        }
        state.clock[tuple.first].step();

        Relay to_send;
        to_send.key = tuple.first;
        to_send.value = tuple.second;
        to_send.replica_id = sid;
        to_send.clock = state.clock[tuple.first].state();

        // Send relay to each node
        for (auto& node : nodes) {
          std::string nid = node_id(node.host, node.port);
          if (nid == sid) {
            continue;
          }
          state.replication_out[nid].push(to_send);
        }
      }
    });
  }

 public:
  AsyncReplicatedContainer(ReplicationConfig& config) {
    sid = node_id(config.host, config.port);
    reader_port = config.port;
    delay = config.delay;
    nodes = config.nodes_list;
    is_verbose = config.is_verbose;
    show_network_err = config.show_network_errors;
    max_waits = config.max_waits;

    for (size_t i = 0; i < nodes.size(); i++) {
      if (nodes[i].host == config.host && nodes[i].port == config.port) {
        clock_id = i;
        break;
      }
    }
  }
  ~AsyncReplicatedContainer() { stop(); }

  void connection_handler(uint16_t port) {
    std::vector<std::thread> readers;

    while (true) {
      if (state.ImmutableUse([](SharedState const& state) { return state.die; })) {
        break;
      }
      try {
        current::net::Socket socket((current::net::BarePort(port)));
        current::net::Connection connection(socket.Accept());

        if (is_verbose) {
          state.MutableUse([port](SharedState&) { std::cout << "Reader connected on port " << port << std::endl; });
        }

        readers.push_back(std::thread([this, conn = std::make_unique<current::net::Connection>(std::move(connection))] {
          replication_reader(std::move(conn));
        }));
      } catch (const current::Exception& e) {
        if (show_network_err) {
          state.MutableUse([&e](SharedState&) {
            std::cout << "error reader"
                      << ": " << e.OriginalDescription() << std::endl;
          });
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    for (auto& reader : readers) {
      reader.join();
    }
  }

  void replication_reader(const std::unique_ptr<current::net::Connection>& connection) {
    uint32_t waits = 0;
    Relay buffer;

    while (waits < max_waits) {
      if (state.ImmutableUse([](SharedState const& state) { return state.die; })) {
        break;
      }
      try {
        // Get the data size
        buffer = recv_relay(connection);
        state.MutableUse([&buffer, this](SharedState& state) {
          bool is_insert = state.data.find(buffer.key) == state.data.end();
          if (is_insert) {
            state.clock[buffer.key] = CLOCK_T(buffer.clock, clock_id);
          }
          bool is_valid_update = state.clock[buffer.key].merge(buffer.clock, is_insert);
          if (is_valid_update) {
            state.data[buffer.key] = buffer.value;
          }

          if (is_verbose) {
            if (is_insert) {
              std::cout << "NEW [" << buffer.replica_id << "] key " << buffer.key << std::endl;
            } else if (is_valid_update) {
              std::cout << "REPLICATED [" << buffer.replica_id << "] key " << buffer.key << std::endl;
            } else {
              std::cout << "IGNORED [" << buffer.replica_id << "] key " << buffer.key << std::endl;
            }
          }
        });
      } catch (const current::Exception& e) {
        waits += 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      }
    }
  }

  Relay recv_relay(const std::unique_ptr<current::net::Connection>& c) {
    // Get buffer length
    size_t buffer_len;
    auto size = c->BlockingRead(reinterpret_cast<uint8_t*>(&buffer_len), sizeof(size_t));
    if (!size) throw;
    buffer_len = be64toh(buffer_len);

    // Get the key
    std::vector<uint8_t> key_buf(buffer_len);
    size = c->BlockingRead(&key_buf[0], buffer_len);
    if (!size) throw;
    std::string key(key_buf.begin(), key_buf.end());

    // Get the value
    VALUE_T value = VALUE_T::recv(c);

    // Get replica_id length
    size_t repl_len;
    size = c->BlockingRead(reinterpret_cast<uint8_t*>(&repl_len), sizeof(repl_len));
    if (!size) throw;
    repl_len = be64toh(repl_len);

    // Get the replica id
    std::vector<uint8_t> repl_id_buf(repl_len);
    size = c->BlockingRead(&repl_id_buf[0], repl_len);
    if (!size) throw;
    std::string replica_id(repl_id_buf.begin(), repl_id_buf.end());

    // Get the clock vector
    Clocks clock(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
      uint64_t clock_int;
      size = c->BlockingRead(reinterpret_cast<uint8_t*>(&clock_int), sizeof(clock_int));
      if (!size) throw;
      clock_int = be64toh(clock_int);
      clock[i] = clock_int;  // std::chrono::milliseconds(clock_int);
    }

    // Prepare the relay object
    Relay result;
    result.key = key;
    result.value = value;
    result.replica_id = replica_id;
    result.clock = clock;

    return result;
  }

  void send_relay(Relay& r, current::net::Connection& c) {
    // Send key buffer size
    size_t key_len = htobe64(size_t(r.key.length()));
    c.BlockingWrite(&key_len, sizeof(key_len), true);

    // Send the key buffer
    c.BlockingWrite(r.key.c_str(), r.key.length(), true);

    // Send the value
    r.value.send(c);

    // Send replica id buffer size
    key_len = htobe64(size_t(r.replica_id.length()));
    c.BlockingWrite(&key_len, sizeof(key_len), true);

    // Send the replica id buffer
    c.BlockingWrite(r.replica_id.c_str(), r.replica_id.length(), true);

    // Send vector clock for given key/value
    for (size_t i = 0; i < nodes.size(); i++) {
      uint64_t time_data = htobe64(r.clock[i]);
      c.BlockingWrite(&time_data, sizeof(time_data), true);
    }
  }

  void writer(std::string host, uint16_t port) {
    struct MsgOrDie {
      bool die;
      Relay to_send;
    };
    bool is_stop = false;
    Relay buffer;
    std::string queue_id = node_id(host, port);

    while (true) {
      is_stop = state.ImmutableUse([](SharedState const& state) { return state.die; });
      if (is_stop) {
        break;
      }
      try {
        current::net::Connection conn(current::net::ClientSocket(host, port));

        if (is_verbose) {
          state.MutableUse([port](SharedState&) { std::cout << "Writer connected on port " << port << std::endl; });
        }
        while (true) {
          auto data_or_die = state.WaitFor(
              [&queue_id](SharedState const& state) {
                return state.die || !state.replication_out.at(queue_id).empty();
              },
              [this, &queue_id](SharedState& state) {
                if (state.die) {
                  return MsgOrDie{state.die, Relay()};
                }
                auto data = state.replication_out[queue_id].front();
                state.replication_out[queue_id].pop();
                state.clock[data.key].step();
                data.clock = state.clock[data.key].state();
                data.value = state.data[data.key];

                // send_relay(data, conn);
                MsgOrDie result = MsgOrDie{false, std::move(data)};
                return result;
              },
              std::chrono::milliseconds(1));
          if (data_or_die.die) {
            break;
          }
          // If WaitFor returns data send it
          if (data_or_die.to_send.key != "") {
            send_relay(data_or_die.to_send, conn);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
      } catch (const current::Exception& e) {
        if (show_network_err) {
          state.MutableUse([&e](SharedState&) {
            std::cout << "error writer"
                      << ": " << e.OriginalDescription() << std::endl;
          });
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
  }

  void start() {
    for (auto& node : nodes) {
      std::string nid = node_id(node.host, node.port);
      if (nid == sid) {
        continue;
      }

      state.MutableUse([this, &nid](SharedState& state) { state.replication_out[nid] = std::queue<Relay>(); });
      writers.push_back(std::thread([this, &node] { writer(node.host, node.port); }));
      if (is_verbose) {
        std::cout << "Replicated with node " << nid << " with clock_id " << clock_id << std::endl;
      }
    }
    reader = std::thread([this] { connection_handler(reader_port); });
    is_ready = true;
  }
  void stop() {
    if (!is_ready) {
      return;
    }
    // Set die state
    state.MutableUse([](SharedState& state) { state.die = true; });
    // Join replication threads
    reader.join();
    for (auto& w : writers) {
      w.join();
    }
    // Join data monitor
    monitor.join();

    is_ready = false;
    if (is_verbose) {
      std::cout << "Replication has been stopped" << std::endl;
    }
  }

  // Wrapper to set new value and replicate
  void set(std::pair<std::string, VALUE_T> tuple) {
    if (is_verbose) {
      std::cout << "SET key " << tuple.first << std::endl;
    }
    update(tuple, true);
  }

  VALUE_T get(std::string key) {
    if (!is_ready) {
      throw std::logic_error("Replication is not ready");
    }
    auto res = state.ImmutableUse([&key](const SharedState& state) { return state.data.at(key); });
    return res;
  }

  Relay get_info(std::string key) {
    if (!is_ready) {
      throw std::logic_error("Replication is not ready");
    }
    auto res = state.MutableUse([&key, this](SharedState& state) {
      auto val = state.data.at(key);
      auto clock = state.clock[key].state();
      Relay res;
      res.key = key;
      res.value = val;
      res.replica_id = sid;
      res.clock = clock;
      return res;
    });
    return res;
  }

  bool contains(std::string key) {
    if (!is_ready) {
      throw std::logic_error("Replication is not ready");
    }
    auto res =
        state.ImmutableUse([&key](const SharedState& state) { return state.data.find(key) != state.data.end(); });
    return res;
  }

  void start_monitor(std::vector<std::string>& keys, uint32_t mon_delay = 500) {
    // For debugging only
    monitor = std::thread([&keys, mon_delay, this] {
      bool stop = false;
      while (!stop) {
        for (auto& key : keys) {
          if (!contains(key)) {
            continue;
          }
          auto info = get_info(key);
          stop = state.MutableUse([&info, this](SharedState& state) {
            std::cout << "key= " << info.key << " val= " << info.value.data << " clock= ";
            for (size_t i = 0; i < state.clock[info.key].state().size(); i++) {
              std::cout << info.clock[i] << " ";
            }
            std::cout << std::endl;
            return state.die;
          });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(mon_delay));
      }
    });
  }
};
