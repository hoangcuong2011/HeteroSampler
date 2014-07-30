#ifndef POS_MODEL_H
#define POS_MODEL_H

#include "tag.h"
#include "corpus.h"
#include "objcokus.h"
#include "log.h"
#include "MarkovTree.h"

#include <vector>
#include <list>
#include <thread>
#include <condition_variable>

struct Model {
public:
  Model(const Corpus& corpus, int T = 1, int B = 0, int Q = 10, double eta = 0.5);
  virtual void run(const Corpus& testCorpus);
  double test(const Corpus& corpus);

  /* gradient interface */
  virtual ParamPointer gradient(const Sentence& seq) = 0; 
  virtual TagVector sample(const Sentence& seq) = 0;

  /* stats utils */
  FeaturePointer tagEntropySimple() const;
  FeaturePointer wordFrequencies() const;
  std::pair<Vector2d, std::vector<double> > tagBigram() const;

  /* parameters */
  int T, B, Q, Q0;
  double testFrequency;
  double eta;
  std::vector<objcokus> rngs;
  const Corpus& corpus;
  ParamPointer param, G2, stepsize;   // model.

protected:
  void adagrad(ParamPointer gradient);
  void configStepsize(ParamPointer gradient, double new_eta);

  int K;          // num of particle. 

  XMLlog xmllog;
};

struct ModelSimple : public Model {
public:
  using Model::Model;
  void run(const Corpus& testCorpus, bool lets_test);
  ParamPointer gradient(const Sentence& seq, TagVector* vec = nullptr, bool update_grad = true);
  ParamPointer gradient(const Sentence& seq);
  TagVector sample(const Sentence& seq); 
};

struct ModelCRFGibbs : public Model {
public:
  using Model::Model;
  ParamPointer gradient(const Sentence& seq, TagVector* vec = nullptr, bool update_grad = true);
  ParamPointer gradient(const Sentence& seq);
  TagVector sample(const Sentence& seq);
};

struct ModelIncrGibbs : public Model {
public:
  using Model::Model;
  ParamPointer gradient(const Sentence& seq, TagVector* vec = nullptr, bool update_grad = true);
  ParamPointer gradient(const Sentence& seq);
  TagVector sample(const Sentence& seq);
};

struct ModelTreeUA : public Model {
public:
  ModelTreeUA(const Corpus& corpus, int K);

  void run(const Corpus& testCorpus);

  std::shared_ptr<MarkovTree> explore(const Sentence& seq);
  ParamPointer gradient(const Sentence& seq);
  TagVector sample(const Sentence& seq);

  double score(const Tag& tag);

  /* parameters */
  double eps, eps_split;

  /* parallel environment */
  virtual void workerThreads(int tid, int seed, std::shared_ptr<MarkovTreeNode>, Tag tag, objcokus rng);
  std::vector<std::shared_ptr<std::thread> > th;
  std::list<std::tuple<int, std::shared_ptr<MarkovTreeNode>, Tag, objcokus> > th_work;
  size_t active_work;
  std::mutex th_mutex;
  std::condition_variable th_cv, th_finished;
  std::vector<std::shared_ptr<std::stringstream> > th_stream;
  std::vector<std::shared_ptr<XMLlog> > th_log;

private:
  void initThreads(size_t numThreads);
};


struct ModelAdaTree : public ModelTreeUA {
public:
  ModelAdaTree(const Corpus& corpus, int K, double c, double Tstar);
  /* implement components necessary */  
  void workerThreads(int tid, int seed, std::shared_ptr<MarkovTreeNode> node, 
			Tag tag, objcokus rng);
  /* extract posgrad and neggrad for stop-or-not logistic regression */
  std::tuple<double, ParamPointer, ParamPointer, ParamPointer> logisticStop
    (std::shared_ptr<MarkovTreeNode> node, const Sentence& seq, const Tag& tag); 

  FeaturePointer extractStopFeatures
    (std::shared_ptr<MarkovTreeNode> node, const Sentence& seq, const Tag& tag);

  double score(std::shared_ptr<MarkovTreeNode> node, const Tag& tag);

  /* parameters */
  double etaT;
private:
  FeaturePointer wordent, wordfreq;
  Vector2d tag_bigram;
  std::vector<double> tag_unigram_start;
  double m_c, m_Tstar;
};
#endif
