## Async replication example
Vector clock implementation in `AsyncReplicatedContainer`

Example Usage:
1. `make` to build the node
2. Run multiple nodes on different terminals `./.current/node --port=8881`

Usage:
```c++
  ReplicationConfig conf = ReplicationConfig{
      "127.0.0.1",
      8881,
      std::vector<ReplicationNode>{
          ReplicationNode{"127.0.0.1", uint16_t(8881)},
          ReplicationNode{"127.0.0.1", uint16_t(8882)},
      },
      500,    // replication delay
      true,   // verbose
      false,  // show network errors
      10      // max replica waits (disconnects)
  };

  // configure and start
  AsyncReplicatedContainer storage(conf);
  storage.start();
  
  // write/read values
  storage.set(std::pair<std::string, int>("hello", 123));
  auto val = storage.get("hello");
  // check if value exists
  if(storage.contains("test"){
     std::cout << "OK" << std::endl;
  }
  // get detailed info
  auto info = stoage.get_info("hello");
  std::cout << "key= " << info.key;
  std::cout << " val= " << info.value;
  std::cout << " node= " << info.replica_id;
  std::cout << " clock= " << info.clock.count() << std::endl;
  
  storage.stop()
```
