#include <memory>
#include <mutex>

#include "../../blocks/http/api.h"
#include "../../bricks/dflags/dflags.h"
#include "../../bricks/sync/waitable_atomic.h"

DEFINE_uint16(port, 8080, "The local port to use.");
DEFINE_uint32(n, 3, "Max number of audio streams");

struct SharedState final {
  bool die = false;
  std::unordered_set<std::string> to_kill;
  std::map<std::string, std::thread> threads;
  std::map<std::string, std::string> channel_control;
  bool channel_exists(const std::string& channel_id) const{
    return channel_control.find(channel_id) != channel_control.end();
  }
};

struct ControlSignal final {
  // TODO:
  // think about/implement input/output channel switch command (for call redirects)
  bool stop = false;
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  try {
    auto& http = HTTP(current::net::AcquireLocalPort(FLAGS_port));

    current::WaitableAtomic<SharedState> safe_state;

    // Need to join and remove finished sessions
    auto joiner = std::thread([&safe_state]{
      while(true) {
        auto die = safe_state.MutableUse([](SharedState& state){
          if(state.die){
            return true;
          }
          std::vector<std::string> to_join;
          for (auto& pair : state.threads) {
            std::string channel_id = pair.first;
            if (!state.channel_exists(channel_id)) {
              to_join.push_back(pair.first);
              pair.second.join();
            }
          }
          for(auto& to_drop: to_join){
            state.threads.erase(to_drop);
          }
          return false;
        });
        if(die){
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    });

    auto scope = http.Register("/", [&safe_state](Request r) {
      std::string channel_id = r.body;
      // TODO: do not forget to pass input/output port for channel creation handler

      struct channel_validation{
        bool is_valid = false;
        std::string msg;
      };

      auto node_state = safe_state.MutableUse([channel_id, &safe_state](SharedState& state) {
        if(state.channel_exists(channel_id)){
          return channel_validation{false, "error: channel already exists\n"};
        }
        if(state.channel_control.size() == FLAGS_n){
          return channel_validation{false, "error: too many channels\n"};
        }
        // Add channel
        state.channel_control[channel_id] = "";

        state.threads[channel_id] = std::thread([&safe_state, channel_id]() {
          std::cout << "Channel '" << channel_id << "' is online" << std::endl;
          while(true) {
            auto control = safe_state.WaitFor(
                [channel_id](SharedState const& state) {
                  return state.channel_exists(channel_id);
                },
                [channel_id](SharedState & state) {
                  auto iter = state.to_kill.find(channel_id);
                  if (iter != state.to_kill.end()) {
                    state.channel_control.erase(channel_id);
                    state.to_kill.erase(iter);
                    return ControlSignal{true};
                  }
                  return ControlSignal{false};
                },
                // TODO: reduce this time (only for debug)
                std::chrono::seconds(1));
            if(control.stop) {
              std::cout << "Channel '" << channel_id << "' has been stopped" << std::endl;
              break;
            }

            // TODO: route the stream here
            // streaming_sockets example could perfectly fit there
            std::cout << "[" << channel_id << "] worker tick" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
          }
        });
        return channel_validation{true, "created"};
      });
      r(node_state.msg, (node_state.is_valid ? HTTPResponseCode.OK : HTTPResponseCode.BadRequest));
    });

    scope += http.Register("/kill", [&safe_state](Request r) {
      std::string channel_id = r.body;
      auto has_channel = safe_state.ImmutableUse([channel_id](const SharedState& state) {
        return state.channel_exists(channel_id);
      });
      if(!has_channel){
        r("Error: unknown channel\n");
        return;
      }
      // Send kill signal to the channel
      safe_state.MutableUse([channel_id](SharedState& state) {
        state.to_kill.insert(channel_id);
      });
      r("channel killed\n");
    });

    scope += http.Register("/stop", [&safe_state](Request r){
      safe_state.MutableUse([](SharedState& state) {
        state.die = true;
      });
      r("server stop\n");
    });

    std::cout << "listening  up to " << FLAGS_n << " streams on port " << FLAGS_port << std::endl;
    joiner.join();
    std::cout << "Safe shutdown" << std::endl;
  }
  catch (current::net::SocketBindException const&) {
    std::cout << "the local port " << FLAGS_port << " is already taken" << std::endl;
  }
}