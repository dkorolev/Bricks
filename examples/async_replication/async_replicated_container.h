#pragma once

#include "../../bricks/sync/waitable_atomic.h"
#include "../../blocks/http/api.h"
#include "../../bricks/time/chrono.h"

#include "vector_clock.h"

#include <sstream>
#include <queue>

CURRENT_STRUCT(Relay) {
  CURRENT_FIELD(key, std::string);
  CURRENT_FIELD(value, uint32_t);
  CURRENT_FIELD(replica_id, std::string);
  CURRENT_FIELD(clock, Clocks);
};

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

template <class CLOCK_T>
class AsyncReplicatedContainer {
 private:
  struct SharedState final {
    bool die = false;
    std::map<std::string, uint32_t> data;
    std::map<std::string, CLOCK_T> clock;
    std::map<std::string, std::queue<std::pair<std::string, uint32_t> > > replication_out;
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
  void update(std::pair<std::string, uint32_t> tuple, bool replicate) {
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

        // Send relay to each node
        for (auto& node : nodes) {
          std::string nid = node_id(node.host, node.port);
          if (nid == sid) {
            continue;
          }
          state.replication_out[nid].push(std::move(tuple));
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
            state.clock[buffer.key] = CLOCK_T(nodes.size(), clock_id);
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
    uint32_t value;
    size = c->BlockingRead(reinterpret_cast<uint8_t*>(&value), sizeof(value));
    if (!size) throw;
    value = ntohl(value);

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
      clock[i] = std::chrono::microseconds(clock_int);
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
    uint32_t value = htonl(r.value);
    c.BlockingWrite(&value, sizeof(value), true);

    // Send replica id buffer size
    key_len = htobe64(size_t(r.replica_id.length()));
    c.BlockingWrite(&key_len, sizeof(key_len), true);

    // Send the replica id buffer
    c.BlockingWrite(r.replica_id.c_str(), r.replica_id.length(), true);

    // Send vector clock for given key/value
    for (size_t i = 0; i < nodes.size(); i++) {
      uint64_t time_data = htobe64(r.clock[i].count());
      c.BlockingWrite(&time_data, sizeof(time_data), true);
    }
  }

  void writer(std::string host, uint16_t port) {
    struct MsgOrDie {
      bool die;
      std::pair<std::string, int> data;
      Clocks clock;
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
                  return MsgOrDie{state.die, std::pair<std::string, int>{}, Clocks()};
                }
                auto data = state.replication_out[queue_id].front();
                state.replication_out[queue_id].pop();

                state.clock[data.first].step();
                auto clock = state.clock[data.first].state();
                MsgOrDie result = MsgOrDie{false, std::move(data), std::move(clock)};
                return result;
              },
              std::chrono::milliseconds(50));
          if (data_or_die.die) {
            break;
          }
          // If WaitFor returns data send it
          if (data_or_die.data.first != "") {
            Relay to_send;
            to_send.key = data_or_die.data.first;
            to_send.value = data_or_die.data.second;
            to_send.replica_id = sid;
            to_send.clock = data_or_die.clock;
            send_relay(to_send, conn);
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

      state.MutableUse([this, &nid](SharedState& state) {
        state.replication_out[nid] = std::queue<std::pair<std::string, uint32_t> >();
      });
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
  void set(std::pair<std::string, uint32_t> tuple) {
    if (is_verbose) {
      std::cout << "SET key " << tuple.first << std::endl;
    }
    update(tuple, true);
  }

  int get(std::string key) {
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
            std::cout << "key= " << info.key << " val= " << info.value << " clock= " << info.clock[clock_id].count()
                      << std::endl;
            return state.die;
          });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(mon_delay));
      }
    });
  }
};
