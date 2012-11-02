/** AVI a version without explicit ODG using deterministic infrastructure -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @author M. Amber Hassaan <ahassaan@ices.utexas.edu>
 */

#ifndef AVI_ODG_ORDERED_H
#define AVI_ODG_ORDERED_H

#include "Galois/Galois.h"
#include "Galois/Callbacks.h"
#include "Galois/Runtime/PerThreadStorage.h"
#include "Galois/Runtime/WorkList.h"

#include <boost/iterator/transform_iterator.hpp>

#include <string>
#include <sstream>
#include <limits>
#include <iostream>
#include <fstream>
#include <set>
#include <utility>

#include <cassert>

#include "AuxDefs.h"
#include "AVI.h"
#include "Element.h"

#include "AVIabstractMain.h"

class AVIodgOrdered: public AVIabstractMain {
protected:
  typedef Galois::Graph::FirstGraph<void*,void,true> Graph;
  typedef Graph::GraphNode Lockable;
  typedef std::vector<Lockable> Locks;

  Graph graph;
  Locks locks;

  virtual const std::string getVersion() const {
    return "Parallel version, ODG automatically managed (2)";
  }
  
  virtual void initRemaining(const MeshInit& meshInit, const GlobalVec& g) {
    assert(locks.empty());
    locks.reserve(meshInit.getNumNodes());
    for (int i = 0; i < meshInit.getNumNodes(); ++i) {
      locks.push_back(graph.createNode(0));
    }
  }

  struct Item {
    double timestamp;
    AVI* avi;
    Item(double t, AVI* a): timestamp(t), avi(a) { }
  };

  struct Prefix {
    Graph& graph;
    Locks& locks;

    Prefix(Graph& g, Locks& l): graph(g), locks(l) { }

    void operator()(const Item& item, Galois::UserContext<Item>&) {
      typedef std::vector<GlobalNodalIndex> V;

      const V& conn = item.avi->getGeometry().getConnectivity();

      for (V::const_iterator ii = conn.begin(), ei = conn.end(); ii != ei; ++ii) {
        graph.getData(locks[*ii]);
      }
    }
  };

  struct Process {
    MeshInit& meshInit;
    GlobalVec& g;
    GaloisRuntime::PerThreadStorage<LocalVec>& perIterLocalVec;
    bool createSyncFiles;
    IterCounter& iter;

    Process(
        MeshInit& meshInit,
        GlobalVec& g,
        GaloisRuntime::PerThreadStorage<LocalVec>& perIterLocalVec,
        bool createSyncFiles,
        IterCounter& iter):
      meshInit(meshInit),
      g(g),
      perIterLocalVec(perIterLocalVec),
      createSyncFiles(createSyncFiles),
      iter(iter) { }

    void operator()(const Item& item, Galois::UserContext<Item>& ctx) {
      // for debugging, remove later
      iter += 1;

      LocalVec& l = *perIterLocalVec.getLocal();

      AVIabstractMain::simulate(item.avi, meshInit, g, l, createSyncFiles);

      if (item.avi->getNextTimeStamp() < meshInit.getSimEndTime()) {
        ctx.push(Item(item.avi->getNextTimeStamp(), item.avi));
      }
    }
  };

  struct Compare: public Galois::CompareCallback {
    AVIComparator comp;
    bool operator()(void *a, void *b) const {
      Item* aa = static_cast<Item*>(a);
      Item* bb = static_cast<Item*>(b);
      return comp(aa->avi, bb->avi);
    }
    virtual bool compare(void *a, void *b) const {
      return (*this)(a, b);
    }
  };

  struct MakeItem: public std::unary_function<AVI*,Item> {
    Item operator()(AVI* avi) const { return Item(avi->getNextTimeStamp(), avi); }
  };

public:
  virtual void runLoop(MeshInit& meshInit, GlobalVec& g, bool createSyncFiles) {
    const size_t nrows = meshInit.getSpatialDim();
    const size_t ncols = meshInit.getNodesPerElem();

    GaloisRuntime::PerThreadStorage<LocalVec> perIterLocalVec;
    for (unsigned int i = 0; i < perIterLocalVec.size(); ++i)
      *perIterLocalVec.getRemote(i) = LocalVec(nrows, ncols);

    IterCounter iter;

    Prefix prefix(graph, locks);
    Process p(meshInit, g, perIterLocalVec, createSyncFiles, iter);

    const std::vector<AVI*>& elems = meshInit.getAVIVec();
    Galois::for_each_ordered(
        boost::make_transform_iterator(elems.begin(), MakeItem()),
        boost::make_transform_iterator(elems.end(), MakeItem()),
        prefix, p, Compare());

    printf("iterations = %d\n", iter.reduce());
  }
};

#endif
