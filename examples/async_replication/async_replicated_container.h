#pragma once

#include "../../bricks/sync/waitable_atomic.h"
#include "../../blocks/http/api.h"
#include "../../bricks/time/chrono.h"

#include <sstream>
#include <queue>

#define htonll(x) ((1==htonl(1)) ? (x) : (((uint64_t)htonl((x) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
#define ntohll(x) ((1==ntohl(1)) ? (x) : (((uint64_t)ntohl((x) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))


CURRENT_STRUCT(Relay) {
  CURRENT_FIELD(key, std::string);
  CURRENT_FIELD(value, uint32_t);
  CURRENT_FIELD(replica_id, std::string);
  CURRENT_FIELD(clock, std::chrono::microseconds);
};

struct ReplicationNode final{
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

struct SharedState final {
  bool die = false;
  std::map<std::string, uint32_t > data;
  std::map<std::string, std::chrono::microseconds > clock;
  std::queue<std::pair<std::string, uint32_t> > replication_out;
};


class AsyncReplicatedContainer
{
 private:
  std::string sid;
  uint16_t reader_port;
  bool is_ready = false;
  std::vector<ReplicationNode> nodes;
  current::WaitableAtomic<SharedState> state;
  std::vector<std::thread> writers;
  std::vector<std::thread> readers;
  std::thread monitor;
  uint32_t delay;
  bool is_verbose;
  bool show_network_err;
  uint32_t max_waits;

  std::string node_id(std::string host, uint16_t port){
    std::ostringstream r;
    r << host << ":" << port;
    return r.str();
  }
  std::string node_id(const ReplicationNode& node) {
    return node_id(node.host, node.port);
  }

  // Internal method to apply replication or set new value and replicate
  void update(std::pair<std::string, uint32_t> tuple, bool replicate){
    if(! is_ready){
      throw std::logic_error("Replication is not ready");
    }
    state.MutableUse([tuple, replicate, this](SharedState& state) {
      // Add local data
      state.data[tuple.first] = tuple.second;

      // Set clock and send update to replication thread
      if(replicate) {
        state.clock[tuple.first] = current::time::Now();
        state.replication_out.push(std::move(tuple));
      }
    });
  }

 public:
  AsyncReplicatedContainer(ReplicationConfig &config){
      sid = node_id(config.host, config.port);
      reader_port = config.port;
      delay = config.delay;
      nodes = config.nodes_list;
      is_verbose = config.is_verbose;
      show_network_err = config.show_network_errors;
      max_waits = config.max_waits;
  }
  ~AsyncReplicatedContainer(){
    stop();
  }

  void reader(uint16_t port){
    int waits = 0;
    bool is_stop = false;
    Relay buffer;

    while (true) {
      is_stop = state.ImmutableUse(
          [](SharedState const&state){ return state.die;});
      if(is_stop){
        break;
      }
      try {
        current::net::Socket socket((current::net::BarePort(port)));
        // FIXME? how to accept multiple connections on same port with SO_REUSEADDR/REUSEPORT
        current::net::Connection connection(socket.Accept());

        if(is_verbose) {
          state.MutableUse([port](SharedState& state) {
            std::cout << "Reader connected on port " << port << std::endl;
          });
        }

        while (waits < max_waits) {
          is_stop = state.ImmutableUse(
              [](SharedState const&state){ return state.die;});
          if(is_stop){
            break;
          }
          try {
            // Get the data size
            buffer = recv_relay(connection);

            state.MutableUse([&buffer, this](SharedState& state) {
              std::pair<std::string, int> data(buffer.key, buffer.value);
              bool is_insert = state.data.find(buffer.key) == state.data.end();
              bool is_valid_update = (!is_insert) && (buffer.clock > state.clock[buffer.key]);

              // new row
              if(is_insert || is_valid_update) {
                state.clock[buffer.key] = buffer.clock;
                state.data[buffer.key] = buffer.value;
              }
              if(is_verbose){
                if(is_insert){
                  std::cout << "NEW [" << buffer.replica_id << "] key "<< buffer.key << std::endl;
                } else if (is_valid_update){
                  std::cout << "REPLICATED [" << buffer.replica_id << "] key " << buffer.key << std::endl;
                } else {
                  std::cout << "IGNORED [" << buffer.replica_id << "] key " << buffer.key << std::endl;
                }
              }

            });
          } catch (const current::Exception &e) {
            waits += 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
          }
        }
        waits = 0;
      } catch (const current::Exception &e) {
        if(show_network_err) {
          state.MutableUse([&e](SharedState& state) {
            std::cout << "error reader" << ": " << e.OriginalDescription() << std::endl;
          });
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    // stopped
  }

  Relay recv_relay(current::net::Connection &c){
    // Get buffer length
    char* len_buf = (char*)malloc(sizeof(size_t));
    auto size = c.BlockingRead(len_buf, sizeof(size_t));
    if(!size) throw;
    size_t *buffer_len = reinterpret_cast<size_t*>(len_buf);
    *buffer_len = ntohll(*buffer_len);

    // Get the key
    char* key_buf = (char*)malloc(*buffer_len);
    size = c.BlockingRead(key_buf, *buffer_len);
    if(!size) throw;
    std::string key(key_buf, *buffer_len);

    // Get the value
    char* val_buf = (char*)malloc(sizeof(uint32_t));
    size = c.BlockingRead(val_buf, sizeof(uint32_t));
    if(!size) throw;
    uint32_t *value = reinterpret_cast<uint32_t*>(val_buf);
    *value = ntohl(*value);

    // Get replica_id length
    char* repl_buf = (char*)malloc(sizeof(size_t));
    size = c.BlockingRead(repl_buf, sizeof(size_t));
    if(!size) throw;
    size_t *repl_len = reinterpret_cast<size_t*>(repl_buf);
    *repl_len = ntohll(*repl_len);

    // Get the replica id
    char* repl_id_buf = (char*)malloc(*repl_len);
    size = c.BlockingRead(repl_id_buf, *repl_len);
    if(!size) throw;
    std::string replica_id(repl_id_buf, *repl_len);

    // Get the clock
    char* clock_buf = (char*)malloc(sizeof(uint64_t));
    size = c.BlockingRead(clock_buf, sizeof(uint64_t));
    if(!size) throw;
    uint64_t *clock_int = reinterpret_cast<uint64_t *>(clock_buf);
    *clock_int = ntohll(*clock_int);
    std::chrono::microseconds clock(*clock_int);

    // Prepare the relay object
    Relay result;
    result.key = key;
    result.value = *value;
    result.replica_id = replica_id;
    result.clock = clock;

    // Free memory
    free(len_buf);
    free(key_buf);
    free(val_buf);
    free(repl_buf);
    free(repl_id_buf);
    free(clock_buf);

    return result;
  }

  void send_relay(Relay &r, current::net::Connection &c){
    // Send key buffer size
    size_t key_len = htonll(size_t(r.key.length()));
    c.BlockingWrite(&key_len, sizeof(key_len), true);

    // Send the key buffer
    c.BlockingWrite(r.key.c_str(), r.key.length(), true);

    // Send the value
    uint32_t value = htonl(r.value);
    c.BlockingWrite(&value, sizeof(value), true);

    // Send replica id buffer size
    key_len = htonll(size_t(r.replica_id.length()));
    c.BlockingWrite(&key_len, sizeof(key_len), true);

    // Send the replica id buffer
    c.BlockingWrite(r.replica_id.c_str(), r.replica_id.length(), true);

    // Send clock for given key/value
    uint64_t time_data = htonll(r.clock.count());
    c.BlockingWrite(&time_data, sizeof(time_data), true);
  }

  void writer(std::string host, uint16_t port){
    struct MsgOrDie{
        bool die;
        std::pair<std::string, int> data;
        std::chrono::microseconds clock;
    };
    bool is_stop = false;
    Relay buffer;

    while (true) {
      is_stop = state.ImmutableUse(
          [](SharedState const&state){ return state.die;});
      if(is_stop){
        break;
      }
      try {
        current::net::Connection conn(current::net::ClientSocket(host, port));

        if (is_verbose) {
          state.MutableUse([port](SharedState& state) {
            std::cout << "Writer connected on port " << port << std::endl;
          });
        }
        while(true) {
          auto data_or_die = state.WaitFor(
             [](SharedState const& state) { return state.die || !state.replication_out.empty(); },
             [this](SharedState& state) {
               if(state.die){
                 return MsgOrDie{state.die, std::pair<std::string, int>{}, current::time::Now()};
               }
               auto data = state.replication_out.front();
               state.replication_out.pop();

               auto clock = state.clock[data.first];
               MsgOrDie result = MsgOrDie{false, std::move(data), std::move(clock)};
               return result;
             },
             std::chrono::milliseconds(50));
          if(data_or_die.die){
            break;
          }
          // If WaitFor returns data send it
          if(data_or_die.data.first != "") {
            Relay to_send;
            to_send.key = data_or_die.data.first;
            to_send.value = data_or_die.data.second;
            to_send.replica_id = sid;
            to_send.clock = data_or_die.clock;
            send_relay(to_send, conn);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
      } catch (const current::Exception &e) {
        if(show_network_err) {
          state.MutableUse(
              [&e](SharedState& state) {
                std::cout << "error writer" << ": " << e.OriginalDescription() << std::endl;
              });
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
  }

  void start(){
    for(auto &node : nodes) {
      std::string nid = node_id(node.host, node.port);
      if(nid == sid){
        continue;
      }
      readers.push_back(std::thread([this, &node]{ reader(reader_port); }));
      writers.push_back(std::thread([this, &node]{ writer(node.host, node.port); }));
      if(is_verbose) {
        std::cout << "Replicated with node " << nid << std::endl;
      }
    }
    is_ready = true;
  }
  void stop(){
    if(! is_ready){
      return;
    }
    // Set die state
    state.MutableUse([](SharedState& state) {
      state.die = true;
    });
    // Join replication threads
    for(auto &r: readers){
      r.join();
    }
    for(auto &w: writers){
      w.join();
    }
    // Join data monitor
    monitor.join();

    is_ready = false;
    if(is_verbose){
      std::cout << "Replication has been stopped" << std::endl;
    }
  }

  // Wrapper to set new value and replicate
  void set(std::pair<std::string, uint32_t> tuple){
    if(is_verbose) {
      std::cout << "SET key " << tuple.first << std::endl;
    }
    update(tuple, true);
  }

  int get(std::string key){
    if(!is_ready){
      throw std::logic_error("Replication is not ready");
    }
    auto res = state.ImmutableUse([&key](const SharedState& state) {
      return state.data.at(key);
    });
    return res;
  }

  Relay get_info(std::string key){
    if(!is_ready){
      throw std::logic_error("Replication is not ready");
    }
    auto res = state.MutableUse([&key, this](SharedState& state) {
      auto val = state.data.at(key);
      auto clock = state.clock[key];
      Relay res;
      res.key = key;
      res.value = val;
      res.replica_id = sid;
      res.clock = clock;
      return res;
    });
    return res;
  }

  bool contains(std::string key){
    if(!is_ready){
      throw std::logic_error("Replication is not ready");
    }
    auto res = state.ImmutableUse([&key](const SharedState& state) {
      return state.data.find(key) != state.data.end();
    });
    return res;
  }

  void start_monitor(std::vector<std::string> &keys, uint32_t mon_delay=500){
    // For debugging only
    monitor = std::thread([&keys, mon_delay, this]{
      bool stop = false;
      while(!stop){
        for(auto &key : keys) {
          if(!contains(key)){
            continue;
          }
          auto info = get_info(key);
          stop = state.MutableUse([&info](SharedState& state) {
            std::cout << "key= " << info.key << " val= " << info.value << " clock= " << info.clock.count() << std::endl;
            return state.die;
          });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(mon_delay));
      }
    });
  }
};
