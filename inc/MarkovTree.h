#ifndef POS_MARKOV_TREE
#define POS_MARKOV_TREE

#include "tag.h"
#include "utils.h"

struct MarkovTreeNode {
public:
  MarkovTreeNode(std::shared_ptr<MarkovTreeNode> parent);
  ParamPointer gradient;
  double log_weight;
  int depth;
  std::weak_ptr<MarkovTreeNode> parent; // weak_ptr: avoid cycle in reference count.
  std::vector<std::shared_ptr<MarkovTreeNode> > children;
};

static std::shared_ptr<MarkovTreeNode> makeMarkovTreeNode(std::shared_ptr<MarkovTreeNode> parent) {
  return std::shared_ptr<MarkovTreeNode>(new MarkovTreeNode(parent));
}

struct MarkovTree {
public:
  MarkovTree();
  std::shared_ptr<MarkovTreeNode> root; 

  // returun log(sum(weights of all nodes)).
  double logSumWeights(std::shared_ptr<MarkovTreeNode> node); 
  // return expected value of the gradient (unnormalized).
  std::pair<ParamPointer,double> aggregateGradient(std::shared_ptr<MarkovTreeNode> node, double normalize);
  // return expected gradient.
  ParamPointer expectedGradient();
};

#endif
