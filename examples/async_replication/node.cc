#include "../../bricks/dflags/dflags.h"
#include "async_replicated_container.h"
#include "vector_clock.h"
#include <random>

DEFINE_string(host, "127.0.0.1", "Hostname for the server");
DEFINE_uint16(port, 8881, "Replication port");
DEFINE_uint32(delay, 50, "Replication delay in milliseconds");
DEFINE_uint32(write_delay_min, 7000, "Write min delay in milliseconds");
DEFINE_uint32(write_delay_max, 10000, "Write min delay in milliseconds");
DEFINE_uint32(monitor_delay, 500, "Monitor delay in milliseconds");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);
  std::random_device gen;
  std::vector<std::string> keys = {"test_1", "test_2"};
  std::uniform_int_distribution<> key_rand(0, keys.size() - 1);
  std::uniform_int_distribution<> val_rand(0, 1000);
  std::uniform_int_distribution<> sleep_rand(FLAGS_write_delay_min, FLAGS_write_delay_max);

  ReplicationConfig conf = ReplicationConfig{FLAGS_host,
                                             FLAGS_port,
                                             std::vector<ReplicationNode>{
                                                 ReplicationNode{"127.0.0.1", uint16_t(8881)},
                                                 ReplicationNode{"127.0.0.1", uint16_t(8882)},
                                                 ReplicationNode{"127.0.0.1", uint16_t(8883)},
                                             },
                                             FLAGS_delay,
                                             true,
                                             false,
                                             10};

  AsyncReplicatedContainer<StrictVectorClock> storage(conf);
  storage.start();
  storage.start_monitor(keys, FLAGS_monitor_delay);
  while (true) {
    storage.set(std::pair<std::string, int>(keys[key_rand(gen)], val_rand(gen)));
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_rand(gen)));
  }
  // sighup?
  // storage.stop();
}
