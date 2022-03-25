#ifndef _DYN_FLAT_NW_SCH_
#define _DYN_FLAT_NW_SCH_

#include <vector>
#include "flat_topology.h"
#include "queue.h"
#ifdef USE_GUROBI
#include "gurobi_c++.h"
#endif

#include <cstdlib>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <map>
#include <stack>

template< class T >
class Matrix2D;

template< class T >
std::ostream &operator<<( std::ostream &, const Matrix2D< T > & );

template< class dtype >
class Matrix2D {
 public:
  Matrix2D( size_t n_cols, size_t n_rows ) : n_cols( n_cols ), n_rows( n_rows ), mat( nullptr ) {
    mat = new dtype *[n_rows];
    for ( size_t row = 0; row < n_rows; row ++ )
      mat[ row ] = new dtype[n_cols];
    fill_zeros( );
  }

  virtual ~Matrix2D( ) {
    for ( size_t row = 0; row < n_rows; row ++ )
      delete[] mat[ row ];
    delete[] mat;
  }

  Matrix2D( const Matrix2D & ) = delete;
  Matrix2D &operator=( const Matrix2D & ) = delete;

 private:
  size_t n_cols;
  size_t n_rows;
  dtype **mat;
 public:
  void fill_zeros( );

  dtype get_elem( size_t row, size_t col ) const;

  void set_elem( size_t row, size_t col, const dtype value );

  void add_by( const Matrix2D< dtype > &matrix_2_d );

  void sub_by( const Matrix2D< dtype > &matrix_2_d );

  void mul_by( const dtype &value );

  void add_elem_by( size_t row, size_t col, const dtype value );

  void sub_elem_by( size_t row, size_t col, const dtype value );

  void copy_from( const Matrix2D< dtype > &matrix_2_d );

  void normalize_by_max( );

  friend std::ostream &operator
  <<< dtype >(
  std::ostream &os,
  const Matrix2D< dtype > &d
  );
};

template< class dtype >
void Matrix2D< dtype >::fill_zeros( ) {
  for ( size_t row = 0; row < n_rows; row ++ ) {
    for ( size_t col = 0; col < n_cols; col ++ )
      mat[ row ][ col ] = 0;
  }
}

template< class dtype >
void Matrix2D< dtype >::add_by( const Matrix2D< dtype > &matrix_2_d ) {
  for ( size_t row = 0; row < n_rows; row ++ ) {
    for ( size_t col = 0; col < n_cols; col ++ ) {
      mat[ row ][ col ] += matrix_2_d.get_elem( row, col );
    }
  }
}

template< class dtype >
void Matrix2D< dtype >::sub_by( const Matrix2D< dtype > &matrix_2_d ) {
  for ( size_t row = 0; row < n_rows; row ++ ) {
    for ( size_t col = 0; col < n_cols; col ++ )
      mat[ row ][ col ] -= matrix_2_d.get_elem( row, col );
  }
}

template< class dtype >
void Matrix2D< dtype >::mul_by( const dtype &value ) {
  for ( size_t row = 0; row < n_rows; row ++ ) {
    for ( size_t col = 0; col < n_cols; col ++ )
      mat[ row ][ col ] *= value;
  }
}

template< class dtype >
dtype Matrix2D< dtype >::get_elem( size_t row, size_t col ) const {
  return mat[ row ][ col ];
}

template< class dtype >
void Matrix2D< dtype >::copy_from( const Matrix2D< dtype > &matrix_2_d ) {
//  if ( matrix_2_d.n_cols > n_cols || matrix_2_d.n_rows > n_rows ) {
//    throw std::runtime_error( "matrix dimensions do not match for copying." );
//  }
  size_t min_n_rows = std::min( matrix_2_d.n_rows, n_rows );
  size_t min_n_cols = std::min( matrix_2_d.n_cols, n_cols );
  for ( size_t row = 0; row < min_n_rows; row ++ ) {
    for ( size_t col = 0; col < min_n_cols; col ++ )
      mat[ row ][ col ] = matrix_2_d.get_elem( row, col ); //todo:use memcpy
  }
}

template< class dtype >
void Matrix2D< dtype >::normalize_by_max( ) {
  dtype max_data = std::numeric_limits< dtype >::min( );
  for ( size_t row = 0; row < n_rows; row ++ ) {
    for ( size_t col = 0; col < n_cols; col ++ )
        if ( row != col )
            max_data = ( max_data < mat[ row ][ col ] ? mat[ row ][ col ] : max_data );
  }
  if ( max_data != 0 ) {
    for ( size_t row = 0; row < n_rows; row ++ ) {
      for ( size_t col = 0; col < n_cols; col ++ ) {
        mat[ row ][ col ] /= max_data;
        //mat[ row ][ col ] = double( int( 100. * mat[ row ][ col ] ) ) / 100.;
        mat[ row ][ col ] = double( mat[ row ][ col ] );
      }
    }
  }
}

template< class dtype >
void Matrix2D< dtype >::set_elem( size_t row, size_t col, const dtype value ) {
  mat[ row ][ col ] = value;
}

template< class dtype >
void Matrix2D< dtype >::add_elem_by( size_t row, size_t col, const dtype value ) {
  mat[ row ][ col ] += value;
}

template< class dtype >
void Matrix2D< dtype >::sub_elem_by( size_t row, size_t col, const dtype value ) {
  mat[ row ][ col ] -= value;
}

template< class dtype >
std::ostream &operator<<( std::ostream &os, const Matrix2D< dtype > &d ) {
  for ( size_t row = 0; row < d.n_rows; row ++ ) {
    for ( size_t col = 0; col < d.n_cols; col ++ )
      os << d.mat[ row ][ col ] << " ";
    os << std::endl;
  }
  return os;
}

template< class NodeType >
class Graph {
 private:
  std::map< NodeType, bool > visited;
 public:
  std::map< NodeType, std::set< NodeType > > adj;
  /* a map to predecessors ( memory redundancy :D )*/
  std::map< NodeType, std::set< NodeType > > reverse_adj;

  std::vector< NodeType > sorted;
 private:
  int dfs( NodeType u, std::stack< NodeType > &stack );

  int find_descendants( NodeType u, std::unordered_map< NodeType, std::unordered_set< NodeType>> &descendants );

 public:
  Graph( ) : visited( ), adj( ), reverse_adj( ), sorted( ) { }

  int add_edge( NodeType u, NodeType v );

  int topological_sort( std::stack< NodeType > &stack );

  virtual int summary( ) const;

  virtual ~Graph( ) = default;

  int descendants_map( std::unordered_map< NodeType, std::unordered_set< NodeType>> &descendants );
};

template< class NodeType >
int Graph< NodeType >::add_edge( NodeType u, NodeType v ) {
  adj[ v ]; /* create dst node if it already doesn't exist */
  adj[ u ].emplace( v );

  /* also create the reverse access graph for dependency check
   * speed-ups */
  reverse_adj[ u ];
  reverse_adj[ v ].emplace( u );
  return 0;
}

template< class NodeType >
int Graph< NodeType >::dfs( NodeType u, std::stack< NodeType > &stack ) {
  visited.at( u ) = true;
  for ( auto v : adj[ u ] ) {
    if ( ! visited.at( v ))
      dfs( v, stack );
  }
  stack.push( u );
  return 0;
}

template< class NodeType >
int Graph< NodeType >::topological_sort( std::stack< NodeType > &stack ) {
  visited.clear( );
  for ( auto i : adj )
    visited.emplace( i.first, false );
  for ( auto v : visited )
    if ( ! v.second )
      dfs( v.first, stack ).ok( );
  return 0;
}

template< class NodeType >
int Graph< NodeType >::find_descendants( NodeType u,
                                                std::unordered_map< NodeType,
                                                                    std::unordered_set< NodeType > > &descendants ) {
  if ( descendants.count( u ) == 0 ) {
    descendants[ u ];
    for ( auto v : adj.at( u )) {
      find_descendants( v, descendants ).ok( );
      descendants[ u ].emplace( v );
      descendants[ u ].insert( descendants.at( v ).begin( ), descendants.at( v ).end( ));
    }
  }
  return 0;
}

template< class NodeType >
int Graph< NodeType >::descendants_map( std::unordered_map< NodeType,
                                                                   std::unordered_set< NodeType > > &descendants ) {
  for ( auto e : adj ) {
    if ( descendants.count( e.first ) == 0 )
      find_descendants( e.first, descendants ).ok( );
  }
  return 0;
}

template< class NodeType >
int Graph< NodeType >::summary( ) const {
  for ( auto n : adj ) {
    std::cout << n.first << ": ";
    for ( auto s : n.second ) {
      std::cout << s << " ";
    }
    std::cout << std::endl;
  }
  return 0;
}

class TcpRtxTimerScanner;

struct DemandRecorder {

  // as a trick, use rtx_scanner to record the tcp flows
  DemandRecorder(int degree, TcpRtxTimerScanner * rtx_scanner);
  
  // void add_demand(int src, int dst, uint64_t bytes);
  // void satisfied(int src, int dst, uint64_t bytes);

  void get_unsatisfied_demand(Matrix2D<double> & tm);

  /* unsatisfied traffic demand over the last reconf_delay */
  int degree;
  TcpRtxTimerScanner * rtx_scanner;
};

class DynFlatScheduler : public EventSource {
public:
  enum DynNetworkStatus {
    DYN_NET_INIT,
    DYN_NET_LIVE,
    DYN_NET_RECONF
  };

  enum OptStrategy {
    SIPML_OCS,
    SIPML_RING,
    D_HEURISTIC
  };

  DynFlatScheduler(int nnodes, int degree, FlatTopology* topo, OptStrategy method,
    DemandRecorder* demandrecorder, simtime_picosec refonc_delay, EventList & eventlist);

  virtual void doNextEvent();

  /* pause every queue. bake in the new topology
   * Set all queue status to "PAUSE_RECEIVED"
   */
  void start_reconf();
  /* send continue events on queues with positive bw; 
   * set state of queue to paused otherwise 
   */
  void finish_reconf();

  void do_reconf();

  void update_all_queue_bandwidth();
  void set_all_queues_pause_recved();
  void set_all_tcp_pause();
  void update_all_route();
  void resume_tcp_flows();
  // void resume_lively_queues();
  // void pause_no_bw_queues();

  void normalize_tm(Matrix2D<double> & normal_tm);

  int nnodes;
  int degree;
  int n_nondelay = 4;
#ifdef USE_GUROBI
  GRBModel *gmodel;
#endif
  FlatTopology* topo;
  simtime_picosec reconf_delay;
  DynNetworkStatus status;
  OptStrategy optstrategy;
  EventList & eventlist;

  DemandRecorder * demandrecorder;

  int non_empty_queues;
};

#endif