/** Betweenness centrality application -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2017, The University of Texas at Austin. All rights reserved.
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
 * Betweeness-centrality.
 *
 * @author Dimitrios Prountzos <dprountz@cs.utexas.edu>
 */

#include "galois/Galois.h"
#include "galois/graphs/LCGraph.h"

#include "llvm/Support/CommandLine.h"
#include "Lonestar/BoilerPlate.h"

#include <boost/iterator/filter_iterator.hpp>

#include <iomanip>
#include <fstream>

static const char* name = "Betweenness Centrality";
static const char* desc = "Computes the betweenness centrality of all nodes in "
                          "a graph";
static const char* url  = "betweenness_centrality";

static llvm::cl::opt<std::string> filename(llvm::cl::Positional, 
                                           llvm::cl::desc("<input file>"), 
                                           llvm::cl::Required);
static llvm::cl::opt<int> iterLimit("limit", 
                                    llvm::cl::desc("Limit number of iterations "
                                                   "to value (0 is all nodes)"), 
                                    llvm::cl::init(0));
static llvm::cl::opt<unsigned int> startNode("startNode", 
                                             llvm::cl::desc("Node to start "
                                                            "search from"), 
                                             llvm::cl::init(0));
static llvm::cl::opt<bool> forceVerify("forceVerify", 
                                       llvm::cl::desc("Abort if not verified; "
                                                      "only makes sense for "
                                                      "torus graphs"));
static llvm::cl::opt<bool> printAll("printAll", 
                                    llvm::cl::desc("Print betweenness values "
                                                   "for all nodes"));

typedef galois::graphs::LC_CSR_Graph<void, void>
  ::with_no_lockable<true>::type
  ::with_numa_alloc<true>::type Graph;
typedef Graph::GraphNode GNode;

class BCouter {
  Graph* G;
  int NumNodes;

  galois::substrate::PerThreadStorage<double*> CB;
  galois::substrate::PerThreadStorage<double*> perThreadSigma;
  galois::substrate::PerThreadStorage<int*> perThreadD;
  galois::substrate::PerThreadStorage<double*> perThreadDelta;
  galois::substrate::PerThreadStorage<galois::gdeque<GNode>*> perThreadSucc;

public:

  BCouter (Graph& g): G(&g), NumNodes(g.size()) {
    InitializeLocal();
  }
  
  ~BCouter (void) {
    DeleteLocal();
  }

  template <typename Cont>
  void run(const Cont& v) {
    galois::do_all(galois::iterate(v), 
        [&] (const GNode& _req) {
          galois::gdeque<GNode> SQ;

          double* sigma = *perThreadSigma.getLocal();
          int* d = *perThreadD.getLocal();
          double* delta = *perThreadDelta.getLocal();
          galois::gdeque<GNode>* succ = *perThreadSucc.getLocal();

          //unsigned int QAt = 0;

          int req = _req;

          sigma[req] = 1;
          d[req] = 1;

          SQ.push_back(_req);
          for (auto qq = SQ.begin(), eq = SQ.end(); qq != eq; ++qq) {
            GNode _v = *qq;
            int v = _v;
            for (auto ii : G->edges(_v, galois::MethodFlag::UNPROTECTED)) {
              GNode _w = G->getEdgeDst(ii);
              int w = _w;
              if (!d[w]) {
                SQ.push_back(_w);
                d[w] = d[v] + 1;
              }
              if (d[w] == d[v] + 1) {
                sigma[w] = sigma[w] + sigma[v];
                succ[v].push_back(_w);
              }
            }
          }

          while (!SQ.empty()) {
            int w = SQ.back();
            SQ.pop_back();

            double sigma_w = sigma[w];
            double delta_w = delta[w];
            auto& slist = succ[w];
            for (auto ii = slist.begin(), ee = slist.end(); ii != ee; ++ii) {
              GNode v = *ii;
              delta_w += (sigma_w/sigma[v])*(1.0 + delta[v]);
            }
            delta[w] = delta_w;
          }

          double* Vec = *CB.getLocal();
          for (int i = 0; i < NumNodes; ++i) {
            Vec[i] += delta[i];
            delta[i] = 0;
            sigma[i] = 0;
            d[i] = 0;
            succ[i].clear();
          }
        },
        galois::loopname("Main"));
        
  }

  // Verification for reference torus graph inputs. 
  // All nodes should have the same betweenness value.
  void verify() {
    double sampleBC = 0.0;
    bool firstTime = true;
    for (int i = 0; i < NumNodes; ++i) {
      double bc = (*CB.getRemote(0))[i];
      for (unsigned j = 1; j < galois::getActiveThreads(); ++j)
        bc += (*CB.getRemote(j))[i];
      if (firstTime) {
        sampleBC = bc;
        std::cerr << "BC: " << sampleBC << "\n";
        firstTime = false;
      } else {
        if (!((bc - sampleBC) <= 0.0001)) {
          std::cerr << "If torus graph, verification failed " << (bc - sampleBC) << "\n";
          if (forceVerify)
            abort();
          return;
        }
      }
    }
  }

  void printBCValues(size_t begin, size_t end, std::ostream& out, int precision = 6) {
    for (; begin != end; ++begin) {
      double bc = (*CB.getRemote(0))[begin];
      for (unsigned j = 1; j < galois::getActiveThreads(); ++j)
        bc += (*CB.getRemote(j))[begin];
      out << begin << " " << std::setiosflags(std::ios::fixed) << std::setprecision(precision) << bc << "\n"; 
    }
  }

  void printBCcertificate() {
    std::stringstream foutname;
    foutname << "outer_certificate_" << galois::getActiveThreads();
    std::ofstream outf(foutname.str().c_str());
    std::cerr << "Writing certificate...\n";

    printBCValues(0, NumNodes, outf, 9);

    outf.close();
  }

private:

  template<typename T>
  void initArray(T** addr) {
    *addr = new T[NumNodes]();
  }

  template<typename T>
  void deleteArray(T** addr) {
    delete [] *addr;
  }

  void InitializeLocal(void) {
    galois::on_each(
        [this] (unsigned, unsigned) {
          this->initArray(CB.getLocal());
          this->initArray(perThreadSigma.getLocal());
          this->initArray(perThreadD.getLocal());
          this->initArray(perThreadDelta.getLocal());
          this->initArray(perThreadSucc.getLocal());
          
        });
  }

  void DeleteLocal(void) {
    galois::on_each(
        [this] (unsigned, unsigned) {
          this->deleteArray(CB.getLocal());
          this->deleteArray(perThreadSigma.getLocal());
          this->deleteArray(perThreadD.getLocal());
          this->deleteArray(perThreadDelta.getLocal());
          this->deleteArray(perThreadSucc.getLocal());
        });
  }

};

struct HasOut: public std::unary_function<GNode,bool> {
  Graph* graph;
  HasOut(Graph* g): graph(g) { }
  bool operator()(const GNode& n) const {
    return graph->edge_begin(n) != graph->edge_end(n);
  }
};

int main(int argc, char** argv) {
  galois::SharedMemSys Gal;
  LonestarStart(argc, argv, name, desc, url);

  Graph g;
  galois::graphs::readGraph(g, filename);


  BCouter bcOuter(g);

  size_t NumNodes = g.size();

  galois::reportPageAlloc("MeminfoPre");
  galois::preAlloc(galois::getActiveThreads() * NumNodes / 1650);
  galois::reportPageAlloc("MeminfoMid");

  boost::filter_iterator<HasOut,Graph::iterator>
    begin  = boost::make_filter_iterator(HasOut(&g), g.begin(), g.end()),
    end    = boost::make_filter_iterator(HasOut(&g), g.end(), g.end());

  boost::filter_iterator<HasOut,Graph::iterator> begin2 = 
    iterLimit ? galois::safe_advance(begin, end, (int)iterLimit) : end;

  size_t iterations = std::distance(begin, begin2);

  std::vector<GNode> v(begin, begin2);

  std::cout 
    << "NumNodes: " << NumNodes
    << " Start Node: " << startNode 
    << " Iterations: " << iterations << "\n";
  
  galois::StatTimer T;
  T.start();
  bcOuter.run(v);
  T.stop();

  bcOuter.printBCValues(0, std::min(10ul, NumNodes), std::cout, 6);

  if (printAll)
    bcOuter.printBCcertificate();

  if (forceVerify || !skipVerify)
    bcOuter.verify();

  galois::reportPageAlloc("MeminfoPost");

  return 0;
}
