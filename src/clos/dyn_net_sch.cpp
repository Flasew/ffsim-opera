#include "dyn_net_sch.h"
#include "ecnqueue.h"
#include "queue_lossless.h"
#include "tcp.h"
#include <random>

#ifdef USE_GUROBI
#include "gurobi_c++.h"
#endif

#define INSERT_OR_ADD(_map, _key, _val) do {                                \
  if ((_map).find(_key) == (_map).end()) {                                  \
    (_map)[(_key)] = _val;                                                  \
  } else {                                                                  \
    (_map)[(_key)] += _val;                                                 \
  }                                                                         \
} while (0);                                                                \

static std::random_device rd; 
static std::mt19937 gen = std::mt19937(rd()); 
static std::uniform_real_distribution<double> unif(0, 1);

extern uint32_t SPEED;

DemandRecorder::DemandRecorder(int degree, TcpRtxTimerScanner * rtx_scanner)
: degree(degree), rtx_scanner(rtx_scanner)
{
  // unsatisfied_demand = std::vector<uint64_t>(degree * degree, 0);
}

void DemandRecorder::get_unsatisfied_demand(Matrix2D<double> & tm) 
{
  tm.fill_zeros();
  // simtime_picosec now = eventlist().now();
	list<TcpSrc*>::iterator i = rtx_scanner->_tcps.begin();
	while (i != rtx_scanner->_tcps.end())
	{
    TcpSrc * tcpsrc = *i;
		if (tcpsrc->_finished)
		{
			tcpsrc->eventlist().cancelPendingSource(**i);
			// delete *i;
			i = rtx_scanner->_tcps.erase(i);
		}
		else
		{
      tm.add_elem_by(tcpsrc->_flow_src, tcpsrc->_flow_dst, tcpsrc->_flow_size - tcpsrc->_last_acked);
      // std::cerr << "adding " << tcpsrc->_flow_src << ", " << tcpsrc->_flow_dst << ": " << tcpsrc->_flow_size - tcpsrc->_last_acked << std::endl;
      i++;
		}
	}
}

// void DemandRecorder::add_demand(int src, int dst, uint64_t bytes)
// {
//   unsatisfied_demand[src * degree + dst] += bytes;
//   // std::cerr << "added (" << src << ", " << dst << "): " << bytes << std::endl;
// }

// void DemandRecorder::satisfied(int src, int dst, uint64_t bytes)
// {
//   unsatisfied_demand[src * degree + dst] -= bytes;
//   // std::cerr << "removed (" << src << ", " << dst << "): " << bytes << std::endl;
// }

DynFlatScheduler::DynFlatScheduler(int nnodes, int degree, FlatTopology *topo, 
        OptStrategy method, DemandRecorder* demandrecorder, simtime_picosec reconf_delay, EventList &eventlist)
: EventSource(eventlist, "DynFlatScheduler"), nnodes(nnodes), degree(degree), demandrecorder(demandrecorder),
  topo(topo), reconf_delay(reconf_delay), optstrategy(method), eventlist(eventlist)
{
  // demandrecorder.init(nnodes);
  FlatDegConstraintNetworkTopologyGenerator gen{nnodes, degree};
  set_all_queues_pause_recved();
  auto init_conn = gen.generate_topology();
  for ( int src_port = 0; src_port < nnodes; src_port ++ ) {
    for ( int dst_port = 0; dst_port < nnodes; dst_port ++ ) {
      if (src_port == dst_port) continue;
      topo->queues[src_port][dst_port]->_bitrate = init_conn[src_port * nnodes + dst_port] * speedFromMbps((uint64_t)SPEED);
      topo->queues[src_port][dst_port]->_ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / topo->queues[src_port][dst_port]->_bitrate);
    }
  }
  finish_reconf();

  std::cout << "initconn" << std::endl;

  status = DynNetworkStatus::DYN_NET_LIVE;
  eventlist.sourceIsPending(*this, std::min(reconf_delay, timeFromMs(1)));
  if (optstrategy == OptStrategy::SIPML_OCS)
  {
#ifdef USE_GUROBI
    try
    {
      GRBEnv env = GRBEnv( true );

      /* create an empty model */
      try{
        env.start( );
      } catch(GRBException e) {
        cout << "Error code = " << e.getErrorCode() << endl;
        cout << e.getMessage() << endl;
      }
      gmodel = new GRBModel( env );
      Matrix2D<double> normal_tm(nnodes, nnodes);
      normalize_tm(normal_tm);
      //    cout << normal_tm;

      cout << "nnodes" << nnodes << endl;

      gmodel->set(GRB_IntParam_OutputFlag, 0);
      /* create the permutation decisions */
      GRBVar ***perms; /* a degree x nnodes x nnodes binary variable */
      perms = new GRBVar **[degree];
      for (int i = 0; i < degree; i++)
      {
        perms[i] = new GRBVar *[nnodes];
        for (int j = 0; j < nnodes; j++)
        {
          perms[i][j] = new GRBVar[nnodes];
          for (int k = 0; k < nnodes; k++)
          {
            perms[i][j][k] = gmodel->addVar(0.0,
                                            1.0,
                                            0.0,
                                            GRB_BINARY,
                                            "perm_" + to_string(i) +
                                                "_" + to_string(j) +
                                                "_" + to_string(k));
          }
        }
      }

      /* permutation row constraints */
      for (int sw = 0; sw < degree; sw++)
      {
        for (int src = 0; src < nnodes; src++)
        {
          GRBLinExpr expr = 0;
          for (int dst = 0; dst < nnodes; dst++)
          {
            expr += perms[sw][src][dst];
          }
          string s = "egress_constraint_sw" + to_string(sw) + "_port" + to_string(src);
          gmodel->addConstr(expr, GRB_EQUAL, 1.0, s);
        }
      }

      /* permutation column constraints */
      for (int sw = 0; sw < degree; sw++)
      {
        for (int dst = 0; dst < nnodes; dst++)
        {
          GRBLinExpr expr = 0;
          for (int src = 0; src < nnodes; src++)
          {
            expr += perms[sw][src][dst];
          }
          string s = "ingress_constraint_sw" + to_string(sw) + "_port" + to_string(dst);
          gmodel->addConstr(expr, GRB_EQUAL, 1.0, s);
        }
      }

      /* traffic completion time */
      GRBVar min_rate = gmodel->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, "min_rate");
      /* set objective */
      gmodel->setObjective(GRBLinExpr(min_rate), GRB_MAXIMIZE);

      /* create device-to-device bandwidths */
      GRBLinExpr **bw;
      bw = new GRBLinExpr *[nnodes];
      for (int src_dev = 0; src_dev < nnodes; src_dev++)
      {
        bw[src_dev] = new GRBLinExpr[nnodes];
        for (int dst_dev = 0; dst_dev < nnodes; dst_dev++)
        {
          bw[src_dev][dst_dev] = 0;
        }
      }
      for (int src_port = 0; src_port < nnodes; src_port++)
      {
        for (int dst_port = 0; dst_port < nnodes; dst_port++)
        {
          for (int ocs_no = 0; ocs_no < degree; ocs_no++)
          {
            // uint16_t src_dev = src_port; // port_map.at( ocs_no ).at( src_port )->dev_id;
            // uint16_t dst_dev = dst_port; //port_map.at( ocs_no ).at( dst_port )->dev_id;
            bw[src_port][dst_port] += perms[ocs_no][src_port][dst_port];
          }
        }
      }
      for (int src_dev = 0; src_dev < nnodes; src_dev++)
      {
        for (int dst_dev = 0; dst_dev < nnodes; dst_dev++)
        {
          if (normal_tm.get_elem(src_dev, dst_dev) > 0)
          {
            GRBLinExpr rate = bw[src_dev][dst_dev] / normal_tm.get_elem(src_dev, dst_dev);
            string s = "rate_constraint_port" + to_string(src_dev) + "_port" + to_string(dst_dev);
            gmodel->addConstr(rate, GRB_GREATER_EQUAL, min_rate, s);
          }
        }
      }
    }
    catch (GRBException e)
    {
      if (e.getErrorCode() != 10003)
      {
        cout << "Error code = " << e.getErrorCode() << endl;
        cout << e.getMessage() << endl;
      }
    }
#endif
  }
}

void DynFlatScheduler::doNextEvent()
{
  // std::cerr << "At time " << eventlist.now() << " scheduler run, go to " << status << std::endl;
  if (status == DynNetworkStatus::DYN_NET_LIVE)
  {
    start_reconf();
    status = DynNetworkStatus::DYN_NET_RECONF;
    eventlist.sourceIsPendingRel(*this, reconf_delay);
  }
  else
  {
    finish_reconf();
    status = DynNetworkStatus::DYN_NET_LIVE;
    eventlist.sourceIsPendingRel(*this, n_nondelay * reconf_delay);
  }
}

void DynFlatScheduler::start_reconf()
{
  set_all_queues_pause_recved();
  update_all_queue_bandwidth();
}

void DynFlatScheduler::finish_reconf()
{
  // resume_lively_queues();
  // pause_no_bw_queues();
  for (int i = 0; i < nnodes; i++)
  {
    for (int j = 0; j < nnodes; j++)
    {
      if (i == j) continue;
      Queue * q = topo->queues[i][j];
      ECNQueue *eq = dynamic_cast<ECNQueue *>(q);
      // std::cerr << "queue " << i << ", " << j << " br " << eq->_bitrate << " ps per byte " << eq->_ps_per_byte << " size " << eq->_enqueued.size() << std::endl;
      if (eq->_bitrate > 0)
      {
        eq->_state_send = LosslessQueue::READY;
        if (!eq->_enqueued.empty()) {
          eq->beginService();
          // std::cerr << "queue " << i << ", " << j << " br " << eq->_bitrate << " ps per byte " << eq->_ps_per_byte << " size " << eq->_enqueued.size() << std::endl;
          // std::cerr << "\t starting... " << std::endl;
        }
      }
    }
  }
}

void DynFlatScheduler::set_all_queues_pause_recved()
{
  for (int i = 0; i < nnodes; i++)
  {
    for (int j = 0; j < nnodes; j++)
    {
      if (i == j) continue;
      Queue * q = topo->queues[i][j];
      ECNQueue *eq = dynamic_cast<ECNQueue *>(q);
      if (eq->queuesize() > 0)
      {
        eq->_state_send = LosslessQueue::PAUSE_RECEIVED;
      }
      else
      {
        eq->_state_send = LosslessQueue::PAUSED;
      }
    }
  }
}

inline static bool has_tx_endpoint(uint64_t e, size_t v, size_t n) {
  return e / n == v;
}

inline static bool has_rx_endpoint(uint64_t e, size_t v, size_t n) {
  return e % n == v;
}

void DynFlatScheduler::update_all_queue_bandwidth()
{
  // #if SIPML_OCS
  if (optstrategy == OptStrategy::SIPML_OCS)
  {
#ifdef USE_GUROBI
    try
    {
      Matrix2D<double> normal_tm(nnodes, nnodes);
      normalize_tm(normal_tm);
      gmodel->reset(); /* reset solution states */
      /* create device-to-device bandwidths */
      GRBLinExpr **bw;
      bw = new GRBLinExpr *[nnodes];
      for (int src_dev = 0; src_dev < nnodes; src_dev++)
      {
        bw[src_dev] = new GRBLinExpr[nnodes];
        for (int dst_dev = 0; dst_dev < nnodes; dst_dev++)
        {
          bw[src_dev][dst_dev] = 0;
        }
      }
      for (int src_port = 0; src_port < nnodes; src_port++)
      {
        for (int dst_port = 0; dst_port < nnodes; dst_port++)
        {
          for (int ocs_no = 0; ocs_no < degree; ocs_no++)
          {
            // uint16_t src_dev = src_port; //port_map.at( ocs_no ).at( src_port )->dev_id;
            // uint16_t dst_dev = dst_port; //port_map.at( ocs_no ).at( dst_port )->dev_id;
            bw[src_port][dst_port] += gmodel->getVarByName("perm_" + to_string(ocs_no) +
                                                        "_" + to_string(src_port) +
                                                        "_" + to_string(dst_port));
          }
        }
      }

      gmodel->update();
      GRBLinExpr rate;
      for (int src_dev = 0; src_dev < nnodes; src_dev++)
      {
        for (int dst_dev = 0; dst_dev < nnodes; dst_dev++)
        {
          if (normal_tm.get_elem(src_dev, dst_dev) > 0)
          {
            double alpha = normal_tm.get_elem(src_dev, dst_dev);
            rate += (bw[src_dev][dst_dev] * alpha);
          }
        }
      }
      /* set objective */
      gmodel->setObjective(rate, GRB_MAXIMIZE);

      gmodel->update();
      gmodel->optimize();

      // assert(/* TODO */ false);
      // episode_bw.fill_zeros( );
      // double delta = double( cnfg.degree ) * double( cnfg.bwxstep_per_wave ) / double( degree );
      for ( int src_port = 0; src_port < nnodes; src_port ++ ) {
        for ( int dst_port = 0; dst_port < nnodes; dst_port ++ ) {
          if (src_port == dst_port) continue;
          topo->queues[src_port][dst_port]->_bitrate = 0;
          topo->queues[src_port][dst_port]->_ps_per_byte = std::numeric_limits<simtime_picosec>::max();
          for ( int ocs_no = 0; ocs_no < degree; ocs_no ++ ) {
            // uint16_t src_dev = src_port; // port_map.at( ocs_no ).at( src_port )->dev_id;
            // uint16_t dst_dev = dst_port; // port_map.at( ocs_no ).at( dst_port )->dev_id;
            bool is_connected = gmodel->getVarByName( "perm_" + to_string( ocs_no ) +
                "_" + to_string( src_port ) +
                "_" + to_string( dst_port )).get( GRB_DoubleAttr_X );
            if ( is_connected ){
              topo->queues[src_port][dst_port]->_bitrate += speedFromMbps((uint64_t)SPEED);
              topo->queues[src_port][dst_port]->_ps_per_byte = 
                (simtime_picosec)((pow(10.0, 12.0) * 8) / topo->queues[src_port][dst_port]->_bitrate);
            }
          }
        }
      }
    }
    catch (GRBException e)
    {
      if (e.getErrorCode() != 10003)
      {
        cout << "Error code = " << e.getErrorCode() << endl;
        cout << e.getMessage() << endl;
      }
    }
#endif
  }

  else if (optstrategy == OptStrategy::SIPML_RING)
  {
#ifdef USE_GUROBI
    Matrix2D<double> normal_tm(nnodes, nnodes);
    normalize_tm(normal_tm);
    try
    {
      GRBModel mcf_model = GRBModel(GRBEnv());
      mcf_model.set(GRB_IntParam_OutputFlag, 0);
      const double capacity = 1.0;
      uint16_t src;
      uint16_t dst;
      using EdgeWeight = tuple<uint16_t, uint16_t, double>;
      vector<EdgeWeight> flow_weights;
      Graph<uint16_t> flow_graph;
      for (src = 0; src < nnodes; src++)
      {
        for (dst = 0; dst < nnodes; dst++)
        {
          if (normal_tm.get_elem(src, dst) > 0)
          {
            flow_weights.emplace_back(src, dst, -1.0 / normal_tm.get_elem(src, dst));
            flow_graph.add_edge(src, dst);
          }
          else if (dst == (src + 1) % nnodes)
          {
            flow_weights.emplace_back(src, dst, 0); /* dummy edge weight */
            flow_graph.add_edge(src, dst);
          }
        }
      }
      GRBVar *flows;
      flows = new GRBVar[flow_weights.size()];
      for (size_t i = 0; i < flow_weights.size(); i++)
      {
        string var_name = "flow_" +
                          to_string(get<0>(flow_weights[i])) + "to" +
                          to_string(get<1>(flow_weights[i]));
        flows[i] = mcf_model.addVar(0.0, capacity, get<2>(flow_weights[i]), GRB_CONTINUOUS, var_name);
      }
      mcf_model.update();

      /* flow conservation */
      for (auto &flow_weight : flow_weights)
      {
        int node = get<0>(flow_weight);
        GRBLinExpr input_flow = 0;
        for (auto pred : flow_graph.reverse_adj.at(node))
        {
          string fin_name = "flow_" + to_string(pred) + "to" + to_string(node);
          input_flow += mcf_model.getVarByName(fin_name);
        }
        GRBLinExpr output_flow = 0;
        for (auto succ : flow_graph.adj.at(node))
        {
          string fout_name = "flow_" + to_string(node) + "to" + to_string(succ);
          output_flow += mcf_model.getVarByName(fout_name);
        }
        mcf_model.addConstr(input_flow, GRB_EQUAL, output_flow, "flow_conservation_constraint_" + to_string(node));
      }

      /* capacity constraint:
     * the total flow that can pass through each segment is bounded by
     * that segment's capacity */
      for (int seg_no = 0; seg_no < nnodes; seg_no++)
      {
        GRBLinExpr total_flow = 0;
        for (auto &flow_weight : flow_weights)
        {
          src = get<0>(flow_weight);
          dst = get<1>(flow_weight);
          int seg_offset = (seg_no - int(src));
          seg_offset = (seg_offset < 0 ? nnodes - seg_offset : seg_offset);
          int dst_offset = (int(dst) - int(src));
          dst_offset = (dst_offset < 0 ? nnodes - dst_offset : dst_offset);
          if (dst_offset > seg_offset)
          {
            total_flow += mcf_model.getVarByName("flow_" + to_string(src) + "to" + to_string(dst));
          }
        }
        mcf_model.addConstr(total_flow, GRB_LESS_EQUAL, capacity, "capacity_constraint_" + to_string(seg_no));
      }

      /* set objective */
      mcf_model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);

      /* solve */
      mcf_model.optimize();

      /* rounding */
      Matrix2D<uint16_t> allocation(nnodes, nnodes);
      double wave_inv = 1.0 / double(degree);
      for (auto &flow_weight : flow_weights)
      {
        src = get<0>(flow_weight);
        dst = get<1>(flow_weight);
        double alloc = mcf_model.getVarByName("flow_" + to_string(src) + "to" + to_string(dst))
                           .get(GRB_DoubleAttr_X) /
                       wave_inv;
        auto rounded_alloc = uint16_t(alloc);
        double diff = alloc - rounded_alloc;
        double r = ((double)rand() / (RAND_MAX));
        alloc = (r > diff ? alloc : alloc + 1);
        allocation.set_elem(src, dst, alloc);
      }

      /* handle rounding errors */
      for (int seg_no = 0; seg_no < nnodes; seg_no++)
      {
        int total_waves = 0;
        for (auto &flow_weight : flow_weights)
        {
          src = get<0>(flow_weight);
          dst = get<1>(flow_weight);
          int seg_offset = (seg_no - int(src));
          seg_offset = (seg_offset < 0 ? nnodes - seg_offset : seg_offset);
          int dst_offset = (int(dst) - int(src));
          dst_offset = (dst_offset < 0 ? nnodes - dst_offset : dst_offset);
          if (dst_offset > seg_offset)
          {
            total_waves += allocation.get_elem(src, dst);
            /* if exceeding the total number of available waves,
           * deallocate waves until the constraint is met */
            while (total_waves > degree)
            {
              allocation.sub_elem_by(src, dst, 1);
              total_waves -= 1;
            }
          }
        }
      }
      // std::cerr << "allocation: " << allocation << std::endl;

      // TODO episode_bw.mul_by( cnfg.bwxstep_per_wave );
      // assert(/* TODO */ false);
      for ( int src_port = 0; src_port < nnodes; src_port ++ ) {
        for ( int dst_port = 0; dst_port < nnodes; dst_port ++ ) {
          if (src_port == dst_port) continue;
          topo->queues[src_port][dst_port]->_bitrate = allocation.get_elem(src_port, dst_port) * speedFromMbps((uint64_t)SPEED);
          topo->queues[src_port][dst_port]->_ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / topo->queues[src_port][dst_port]->_bitrate);
        }
      }
      delete[] flows;
    }
    catch (GRBException &e)
    {
      cout << "Error code = " << e.getErrorCode() << endl;
      cout << e.getMessage() << endl;
    }
    catch (...)
    {
      cout << "Exception during optimization" << endl;
    }
#endif
  }
  else
  {
    Matrix2D<double> normal_tm(nnodes, nnodes);
    normalize_tm(normal_tm);
    std::vector<uint64_t> conn(nnodes * nnodes, 0);
    // for (int i = 0; i < nnode; i++) {
    //   for (int j = 0; j < nnode; j++) {
    //     size_t eid = edge_id(i, j);
    //     if (logical_traffic_demand.find(eid) != logical_traffic_demand.end()) {
    //       size_t ueid = unordered_edge_id(i, j);
    //       uint64_t traffic_amount = logical_traffic_demand[eid];
    //       if (max_of_bidir.find(ueid) == max_of_bidir.end() 
    //           || traffic_amount > max_of_bidir[ueid]) {
    //         max_of_bidir[ueid] = traffic_amount;
    //       }
    //     }
    //   }
    // }
    std::set<std::pair<double, uint64_t>, std::greater<std::pair<double, uint64_t>>> pq;
    std::unordered_map<size_t, size_t> node_if_allocated_tx;
    std::unordered_map<size_t, size_t> node_if_allocated_rx;
    
    for (int i = 0; i < nnodes; i++) {
      for (int j = 0; j < nnodes; j++) {
      // mod: pre-unscale the demand
        if (normal_tm.get_elem(i, j) > 0)
          pq.insert(std::pair<double, uint64_t>(normal_tm.get_elem(i, j), i * nnodes + j));
      }
    }

    while (pq.size() > 0) {

      std::pair<double, uint64_t> target = *pq.begin();
      pq.erase(pq.begin());

      size_t node0 = target.second / nnodes;
      size_t node1 = target.second % nnodes;
      
      // conn[target.second]++;
      conn[node0 * nnodes + node1]++;
      // conn[edge_id(node1, node0)]++;

      INSERT_OR_ADD(node_if_allocated_tx, node0, 1);
      INSERT_OR_ADD(node_if_allocated_rx, node1, 1);

      target.first /= 2; //*= (double)conn[target.second]/(conn[target.second] + 1);
      if (target.first > 0) {
        pq.insert(target);
      }

      if (node_if_allocated_tx[node0] == degree || node_if_allocated_rx[node1] == degree) {
        for (auto it = pq.begin(); it != pq.end(); ) {
          if (node_if_allocated_tx[node0] == degree && has_tx_endpoint(it->second, node0, nnodes)) {
            // std::cout << "node0 full, removing " << it->second /nnodes << ", " << it->second % nnodes << " with demand left " << it->first << std::endl; 
            it = pq.erase(it);
          }
          else if (node_if_allocated_rx[node1] == degree && has_rx_endpoint(it->second, node1, nnodes)) {
            // std::cout << "node1 full, removing " << it->second /nnodes << ", " << it->second % nnodes << " with demand left " << it->first << std::endl; 
            it = pq.erase(it);
          }
          else {
            ++it;
          }
        } 
      }
    }
    for ( int src_port = 0; src_port < nnodes; src_port ++ ) {
      for ( int dst_port = 0; dst_port < nnodes; dst_port ++ ) {
        if (src_port == dst_port) continue;
        topo->queues[src_port][dst_port]->_bitrate = conn[src_port * nnodes + dst_port] * speedFromMbps((uint64_t)SPEED);
        topo->queues[src_port][dst_port]->_ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / topo->queues[src_port][dst_port]->_bitrate);
      }
    }
  }
}

void DynFlatScheduler::normalize_tm(Matrix2D<double> &normal_tm)
{
  demandrecorder->get_unsatisfied_demand(normal_tm);
  uint64_t max_entry = 0;
  for (int i = 0; i < nnodes; i++) {
    for (int j = 0; j < nnodes; j++) {
      if (i == j) continue;
      Queue * q = topo->queues[i][j];
      ECNQueue *eq = dynamic_cast<ECNQueue *>(q);
      // std::cerr << "queue " << i << ", " << j << " br " << eq->_bitrate << " size " << eq->_enqueued.size() << std::endl;
      // if (eq->_bitrate > 0)
      normal_tm.add_elem_by(i, j, eq->_enqueued.size());
      if (normal_tm.get_elem(i, j) > max_entry) {
        max_entry = normal_tm.get_elem(i, j);
      }
      //       if (demandrecorder.unsatisfied_demand[i * nnodes + j] > max_entry) {
      //   max_entry = demandrecorder.unsatisfied_demand[i * nnodes + j];
      // }
    }
  }
  // if (max_entry == 0) {
  //   max_entry = 1;
  //   for (int i = 0; i < nnodes; i++) {
  //     for (int j = 0; j < nnodes; j++) {
  //       if (i != j && rand() / double(RAND_MAX) > 0.5) normal_tm.add_elem_by(i, j, 1);
  //     }
  //   }
  // }
  normal_tm.mul_by(1.0/(double)max_entry);
  // std::cerr << "normalized tm: " << normal_tm << std::endl;
}


FlatDegConstraintNetworkTopologyGenerator::FlatDegConstraintNetworkTopologyGenerator(int num_nodes, int degree) 
: num_nodes(num_nodes), degree(degree)
{}

std::vector<size_t> FlatDegConstraintNetworkTopologyGenerator::generate_topology() const
{
  std::vector<size_t> conn = std::vector<size_t>(num_nodes*num_nodes, 0);
  
  int allocated = 0;
  int curr_node = 0;
  std::unordered_set<int> visited_node;
  visited_node.insert(0);

  std::uniform_int_distribution<> distrib(0, num_nodes - 1);

  while ((long)visited_node.size() != num_nodes) {
    distrib(gen);
    int next_step = distrib(gen);
    if (next_step == curr_node) {
      continue;
    } 
    if (visited_node.find(next_step) == visited_node.end()) {
      if (conn[get_id(curr_node, next_step)] == degree) {
        continue;
      }
      conn[get_id(curr_node, next_step)]++;
      conn[get_id(next_step, curr_node)]++;
      visited_node.insert(next_step);
      curr_node = next_step;
      allocated += 2;
    }
  }

  assert(allocated == (num_nodes - 1) * 2);

  std::vector<std::pair<int, int> > node_with_avail_if;
  for (int i = 0; i < num_nodes; i++) {
    int if_inuse = get_if_in_use(i, conn);
    if (if_inuse < degree) {
      node_with_avail_if.emplace_back(i, degree - if_inuse);
    }
  }

  distrib = std::uniform_int_distribution<>(0, node_with_avail_if.size() - 1);
  int a = 0, b = 0;

  while (node_with_avail_if.size() > 1) {
    a = distrib(gen);
    while ((b = distrib(gen)) == a);

    assert(conn[get_id(node_with_avail_if[a].first, node_with_avail_if[b].first)] < degree);
    conn[get_id(node_with_avail_if[a].first, node_with_avail_if[b].first)]++;
    conn[get_id(node_with_avail_if[b].first, node_with_avail_if[a].first)]++;
    allocated += 2;

    bool changed = false;
    if (--node_with_avail_if[a].second == 0) {
      if (a < b) {
        b--;
      }
      node_with_avail_if.erase(node_with_avail_if.begin() + a);
      changed = true;
    }
    if (--node_with_avail_if[b].second == 0) {
      node_with_avail_if.erase(node_with_avail_if.begin() + b);
      changed = true;
    }
    if (changed) {
      distrib = std::uniform_int_distribution<>(0, node_with_avail_if.size() - 1);
    }
  }

#ifdef DEBUG_PRINT
  std::cout << "Topology generated: " << std::endl;
  NetworkTopologyGenerator::print_conn_matrix(conn, num_nodes, 0);
#endif
  return conn;
  
}

int FlatDegConstraintNetworkTopologyGenerator::get_id(int i, int j) const
{
  return i * num_nodes + j;
}

int FlatDegConstraintNetworkTopologyGenerator::get_if_in_use(int node, const std::vector<size_t> & conn) const
{
  int result = 0;
  for (int i = 0; i < num_nodes; i++) {
    result += conn[get_id(node, i)];
  }
  return result;
}