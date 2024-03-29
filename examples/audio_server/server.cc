#include <memory>
#include <mutex>

#include "../../blocks/http/api.h"
#include "../../bricks/dflags/dflags.h"
#include "../../bricks/sync/waitable_atomic.h"

DEFINE_uint16(port, 8080, "The local port to use.");
DEFINE_uint32(n, 3, "Max number of audio streams");

const std::string cmd_kill = "kill";

struct SharedState final {
  uint32_t active_channels = 0;
  std::map<std::string, std::string> channel_control;
  bool channel_exists(std::string channel_id) const{
    return channel_control.find(channel_id) != channel_control.end();
  }
};

struct ControlSignal final {
  std::string apply= "";
  bool stop = false;
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  try {
    auto& http = HTTP(current::net::AcquireLocalPort(FLAGS_port));

    current::WaitableAtomic<SharedState> safe_state;
    std::map<std::string, std::thread> threads;

    // Need to join and remove finished sessions
    auto joiner = std::thread([&threads, &safe_state]{
      while(true) {
        std::vector<std::string> to_join;
        for (auto& pair : threads) {
          std::string channel_id = pair.first;
          auto has_channel = safe_state.MutableUse([channel_id](SharedState& state) {
            return state.channel_exists(channel_id);
          });
          if (!has_channel) {
            to_join.push_back(pair.first);
            pair.second.join();
          }
        }
        for(auto& to_drop: to_join){
          threads.erase(to_drop);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    });

    auto scope = http.Register("/", [&safe_state, &threads](Request r) {
      std::string channel_id = r.body;

      auto has_channel = safe_state.MutableUse([channel_id](SharedState& state) { return state.channel_exists(channel_id); });
      if(has_channel){
        r("error: channel already exists\n");
        return;
      }
      uint32_t n_chans = safe_state.MutableUse([](SharedState& state) { return state.active_channels; });
      if(n_chans == FLAGS_n){
        r("error: too many channels\n");
        return;
      }

      // Create new channel
      safe_state.MutableUse([channel_id](SharedState& state) {
        state.channel_control[channel_id] = "";
        state.active_channels++;
      });
      threads[channel_id] = std::thread([&safe_state, channel_id]() {
        std::cout << "Channel '" << channel_id << "' is online" << std::endl;
        while(true) {
          auto control = safe_state.WaitFor(
              [channel_id](SharedState const& state) {
                return state.channel_exists(channel_id);
              },
              [channel_id](SharedState & state) {
                if (state.channel_control[channel_id] == cmd_kill){
                  state.channel_control.erase(channel_id);
                  state.active_channels--;
                  return ControlSignal{"", true};
                }
                return ControlSignal{state.channel_control[channel_id], false};
              },
              // TODO: reduce this time (only for debug)
              std::chrono::seconds(1));
          if(control.stop) {
            std::cout << "Channel '" << channel_id << "' has been stopped" << std::endl;
            break;
          }

          if(!control.apply.empty()) {
            std::cout << channel_id << "received signal " << control.apply << std::endl;
            // TODO: handle control signals here
          }

          // TODO: route the stream here
          std::cout << "[" << channel_id << "] worker tick" << std::endl;

          std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
      });
      r("created\n");
    });

    scope += http.Register("/kill", [&safe_state](Request r) {
      std::string channel_id = r.body;
      auto has_channel = safe_state.MutableUse([channel_id](SharedState& state) { return state.channel_exists(channel_id); });
      if(!has_channel){
        r("Error: unknown channel\n");
        return;
      }
      // Send kill signal to the channel
      safe_state.MutableUse([channel_id](SharedState& state) {
        state.channel_control[channel_id] = cmd_kill;
      });
      r("channel killed\n");
    });

    std::cout << "listening  up to " << FLAGS_n << " streams on port " << FLAGS_port << std::endl;
    http.Join();
    joiner.join();
  }
  catch (current::net::SocketBindException const&) {
    std::cout << "the local port " << FLAGS_port << " is already taken" << std::endl;
  }
}