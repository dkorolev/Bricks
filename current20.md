# Current 2.0

## Goal

The Current 2.0 stack focuses on building durable, highly available singleton services.

**Highly available** means that a machine, or a network to it, may be lost unrecoverably, and the service should keep functioning with no performance degradation and no data loss.

**Singleton** means that, while being highly available, the service acts as a single entity. It can and should be drawn as a single box on a system design diagram, and it behaves as such.

Sharding is deliberately not covered by this design doc. The primary usecase for Current 2.0 are the OLTP applications that handle hundreds of thousands to low millions requests per second. This is what a single thread of a single relatively powerful machine can handle today.

If a service needs to be sharded it is a different conversation, but multiple Current 2.0 singletons can certainly talk to one another on the inner, high-performance level.

## Examples

A basic example is an **authentication service**.

The data model of an auth service contains user accounts, auth tokens issues to them, sessions to be tracked, and a large number of "verify auth token" requests. It makes total sense to design the auth service as a singleton, as long as its throughput allows for it. Current 2.0 makes the claim that a single deployment can handle dozens of millions concurrent users auth-wise. It should be noted though that for auth purposes users are easy to shard.

A more advanced example is a **financial ledger**.

The data model is users, their assets, and various constraints on how these assets can travel between users. The transaction, simply put, is an immutable balance request, or a mutable transfer, that is executed subject to the above constraints. Needless to say, the ledger must be both highly reliable and durable, as it contains sensitive financial data. One shard can verifiably handle low millions of transactions per second, and a design of multiple singletons can be shown to scale horizontally with little overhead, provided that intra-shard transactions settle immediately, and the cross-shard transactions are debited right away, and credited on the next cross-shard sync batch, which can happen as frequently as hundreds of times per second.

## Founding Ideas

### Push Code to Data

To sustain high performance, Current 2.0 **nodes execute code statements on the very machines responsible for keeping cache hot**.

In the case of peak performance, the business logic is JIT-compiled at the edge, together with the cache validation logic. This way, since the vast majority of requests hit the data that is readily available in hot cache, the majority of requests are processed on in-memory data, in the single thread. This eliminates any and all waits and mutexes.

Unless specified otherwise, the incoming requests are only guaranteed to be executed in the order they were received as long as all the data needed to process those requests is available in memory.

If in order to process the request cache should be updated, this request is put into an internal async queue, and only retrieved from there as another thread makes sure the cache is hot. If the requests may need to access multiple data elements that are unavailable yet, the request can be re-queued up again. This is to ensure the main thread is busy 100% of the time processing the requests flow, always operating on the data available in memory, and spending as little time as possible waiting on synchronization primitives.

### Free Recovery

Performance and high availability aside, the key problem Current 2.0 solves is **eliminating the need for complex data recovery logic**.

When a service must guarantee data consistency, generally, its shards keep periodically backing themselves up to the database. The logic to make sure those backups are kept in order can easily be as hard as the logic of the service itself; the logic to make sure the service can recover from those backups can easily be 10x more complex. Moreover, downtime aside, solutions with backing up data to the databases have the administrative problem of only a few engineers in the company knowing how to initiate and orchestrate the process of data recovery if necessary.

Current 2.0 ensures that identical data is kept on several servers; four is the recommended number, but can be a lot larger as necessary. The servers do not have to be in the same geographical location, in fact, for true durability, they can and should be scattered across availability zones, regions, or even continents. The only price the end user would pay is latency; not throughput, as all requests are processed asynchronously.

Since the data kept on all the servers that form the singleton is identical, **it is not necessary for it to be backed up to disk right away**. Checks and balances are put in place so that transactions processing does not outpace disk writes, but, generally, as long as enough machines across the datacenter or across the globe keep the same data in memory, it does not matter if the latency of this data being written to disks is in the order of milliseconds or in the order of seconds. Even in the most extreme circumstances of one server, or one region, being lost forever in an apocalypse scenario, the other nodes of this singleton would have enough time to back the data up.

### Leader Elections

Current 2.0 **nodes talk to one another assuming all of them are aware which of them is presently the leader**.

To ensure high availability via smooth failover, an external, small, leader-elected lock filesystem is used. It is not used frequently, as "committing" each request, or even each requests batch into it would be an overkill.

Instead, a small piece of business logic runs next to this lock filesystem. **As soon as at least two non-leader nodes report they have not heard from the presently active leader node for more than a split second, this lock filesystem elects the new leader and notifies the very edge nodes**. To avoid data corruption, it first turns all the nodes off, freezing any requests processing for a split second, and only then issues the new leader key to the newly elected leader. This way, if the leader machine wakes up and keeps assuming it is a leader, it would not get its requests flow signed by the other nodes, and immediately change its state from the leader to a follower.

### Dynamic Cluster Configuration

New nodes can be added to a running Current 2.0 cluster at runtime.

In fact, it is the routine way both to rotate the existing machines (ex. taking them down for upgrades) and to export data from the cluster.

In the export data case, the new nodes are added to the cluster with two special flags: that they do not participate in possible leader elections, and that they need to get the snapshot of data only until a certain timestamp and/or offset.

This way, as soon as these machines are up to date, they can be safely removed from the clusters, and used for OLAP tasks: be it running local data crunching tasks or populating a remote data warehouse.

## Architecture

The Current 2.0 cluster consists of the components of two distinct kinds:

1. The **Edge Servers**: where the data is stored and where the computation takes place.
2. The **User Servers**: where user requests hit, and where orchestration happens.

### Design

#### Edge Servers

There are several key ideas behind what makes edge servers durable and performant.

1. Edge servers do not attempt to to understand the binary representation of data they operate on. They take the other extreme approach: **each edge server is effectively a free to use 64-bit wide "in-memory" canvas**.

1. Internally, this 64-bit wide space is paginated and cached as needed; but, to the actual "functions", that are the "stored procedures" running as part of the edge server's logic, the canvas is just that: a free-to-use 64-bit wide block of "memory". Thus, **for example, a "hash-map" from a 48-bit ID to a 64K block of data is a perfectly legit usage of the edge server**.

1. Obviously, **each edge server is responsible for keeping its cache hot**. In fact, the actual details of what and how to cache is left to the individual edge server. Caching is and should be multi-level, from RAM / NVRAM, though SSD, all the way to spin disks or even S3 volumes.

1. **The only way to access data on the edge server is via the functions loaded into it from the outside**. In the high-performance case, these functions can be thought of as JIT-compiled pieces of code. During the development and early rollout phases the opposite is true: instead of being optimized and JIT-compiled, these functions are carefully interpreted with all possible checks enabled, i.e. no floating point and integer overflows considered errors.

1. In "OLAP", the offline, read-only mode, "batch" queries, that traverse large chunks of the 64-bit address space, are allowed. In "OLTP", online mode, "batch" queries are strictly prohibited, and, in fact, **there are tight, and strictly enforced, limits on how much work a single query can perform and how many distinct memory locations it can access**. This is to ensure it's not possible to break the functionality of the whole system with a single wrong function.

1. Obviously, **each function must be fully deterministic**.

1. For data harmonization reasons, more on this below in the user servers section, on the edge servers **some regions of the address space can be marked inaccessible for updates and even inaccessible for reads**. This is an extra safety check to make sure data and schema evolution processes are not accidentally wiping out the data they are meant to preserve.

1. **Data encryption**, if needed, is handled on the edge servers as well. What is put into the cold storage is definitely encrypted, and it is possible, as a cost to performance, of course, to encrypt the contents of memory on the edge servers as well.

#### User Servers

The user servers are responsible for two major tasks:

1. They act as **the proxies between the outer world to the edge servers.** Effectively this translates to a) converting input requests, coming from various channels from HTTP/JSON to streamed gRPC, into the raw binary format accepted by the edge servers, and b) parsing the result of how do the edge servers asynchronously process the above requests, and routing it to the respective external clients.

2. They act as **the gatekeepers** of the schema of the data managed by the edge servers. Effectively, it is the user servers that are responsible for "compiling" various data structures and processing functions from the language that operates in high-order types, such as foreign keys, skip lists, priority queues, and free-form strings of potentially arbitrary lengths, to the language that the edge servers speak, which is effectively the assembly language executed upon a "64-bit address space on steroids".

Among the important functions that user servers perform is orchestrating data and schema evolution. Since the edge servers in OLAP (real-time) mode are prohibited from traversing the whole "address space", the recommended evolution process is the following:

* A new version of the existing data structure is introduced.
* The respective part of the address space is marked available for use for the new representation of the data.
* The part of the address space that stores the data in the old format will be marker read-only by the time the update is pushed to the edge server.
* The new getter function uses the following logic:
* * Check whether a record in the new format is available; if it is, just use it.
* * If no record in the new format is available, convert the record from the old format to the new one.
* The above is implemented, pushed, and used for some period of time, say, 24 hours, or a week.
* Then, on a frozen, OLAP, edge server, a batch data crunching job is used to collect the IDs of all old data elements that were not accessed, and thus were not updated.
* The requests to update those elements (dummy gets, in reality) are sent to the edge servers in a throttled fashion, over an extended period of time.
* After this is completed, the not-unused part of the address space is marked inaccessible as well.
* After another week or month of no issues, that part of the address space can be wiped, and re-used later. The data and schema evolution process has been completed.

### Implementation

#### Edge Servers

There are several threads running in each edge computing node.

The main, *processing* thread is processing the requests flow.

Another, *validating* thread responsible for only marking the requests as completed as long as at least N/2 other nodes have confirmed they have executed the same request and got to the same result. This ensures durability: the user would not see their request as fulfilled and responded to until the cluster is more than half in agreement.

A *prefetching* thread is responsible for clearing the input requests with respect to them only accessing the portions of data that are presently in hot cache.

One more, *persisting* thread makes sure that the data gets periodically saved to disk, even if and when it remains hot in cache.

Another important thread is the *replication* thread, that is responsible for bringing other edge servers up to date, if they have just joined the fleet after being offline, and need to catch up on the data, both in their cache and in their cold storage.

Besides, there are several *input* threads responsible for receiving the inbound, binary, requests flow, prepared in the compact formed by the gatekeeper servers.

To complete the picture, there are *telemetry* threads, responsible for making sure the edge server periodically exposes its health status, as well as various performance counters.

Finally, not used in dynamic OLTP mode, but in the frozen OLAP mode, there are *crunching* threads that are allowed to perform long-playing read-only operations on the snapshot of data.

The client to the leader-elected lock filesystem is running on the same machine as each edge server code, but is not part of the very edge server binary. This makes the cluster agnostic as to what exact leader elections engine is used (be it Consul, Zookeeper, etcd, or something else). This separate daemon talks to the very edge server via a compact binary protocol, same as how user servers speak with edge servera overall. In order to prevent network stack overflow, this daemon also talks to the user servers requesting them to stop sending requests to this edge server as this edge server is losing its leader status.

#### User Servers

Unlike the Edge Servers, that are all registered in the leader-elected lock filesystem, the User Servers can and should be sharded horizontally.

Simply put, it is relatively straightforward to handle millions of queries per second if they come in a pre-packaged binary format. It is a lot harder to parse millions of HTTP/JSON requests per second, not to mention those uncompressed requests also require orders of magnitude more traffic.

Since the user servers are responsible for mission-critical functions such as defining the schema and the access methods for the data, the state of these two is considered business-critical and is stored both in the special, metadata, section of the address space of the edge servers, and also in the lock filesystem employed by the cluster. This makes business logic updates slightly slower (potentially dozens of milliseconds as opposed to sub-millisecond latency), but also analyzable and auditable without having to parse the very low-level binary parts of the edge servers' storage schema.
