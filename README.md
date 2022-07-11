# FlexNetPacket simulator

This module contains the FlexNetPacket packet-level network simulator. It models the cluster that runs DNN training job based on TopoOpt, Fat-Tree, SiP-ML, Expander and abstract switch network topologies. The source code was extended from the Opera simulator from NSDI 2020, please check the original README file [here](OPERA_README.md).

## Compilation:
To build the FlexNetPacket simulator, from the top level directory run:
```
export FF_HOME=<directory of FlexNet>
cd src/clos
make
cd datacenter
make
```

## Executables

The executables are found in the `src/clos/datacenter` folder. They have the name "htsim_...". The following table provides details on each executable:

| Executable | Network Topology |
|------------|------------------|
| `htsim_tcp_fattree`       | Fat-Tree network topology, single job |
| `htsim_tcp_flat`          | Flat Network topology (switchless) for single job. Use this executable for TopoOpt and expander |
| `htsim_tcp_fc`            | Fully connected topology for single job |
| `htsim_tcp_os_fattree`    | Oversubscribed Fat-Tree where the ToR switches are oversubscribed |
| `htsim_tcp_aggos_ft`      | Oversubscribed Fat-Tree where the aggregation layer is oversubscribed | 
| `htsim_tcp_dyn_flat`      | Dynamic network used to simulate SiP-ML |
| `htsim_tcp_fattree_multijob` | Fat-Tree network topology used to simulate multiple DNN jobs concurrently |
| `htsim_tcp_aggos_fattree_multijob` | Oversubscribed (aggregation switches) Fat-Tree network topology used to simulate multiple DNN jobs concurrently |

## Brief description on source code

FlexNetPacket's major extention from the htsim simulator allows it to take a taskgraph (in FlatBuffer) generated from FlexNet simulator. To achieve this, `src/clos/ffapp.*` was implemented as an API to read and process these such taskgraphs. In addition, a few network topologies are added, notably the dynamic network executable that simulates SiP-ML. The network scheduler code can be found at `src/clos/dyn_net_sch.*`.

Each topology's "main" function can be found in `src/clos/datacenter/main_tcp_*.cpp`, which provides detailed description on the input arguments for the executable. 

