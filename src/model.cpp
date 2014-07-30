#include "model.h"
#include "utils.h"
#include "log.h"
#include "MarkovTree.h"
#include <iostream>
#include <string>
#include <map>
#include <cmath>
#include <thread>
#include <chrono>

using namespace std;

Model::Model(const Corpus& corpus, int T, int B, int Q, double eta)
:corpus(corpus), param(new map<string, double>()),
  G2(new map<string, double>()) , stepsize(makeParamPointer()), 
  T(T), B(B), K(5), Q(Q), Q0(1),  
  testFrequency(0.3), eta(eta) {
  rngs.resize(K);
}

void Model::configStepsize(ParamPointer gradient, double new_eta) {
  for(const pair<string, double>& p : *gradient) 
    (*stepsize)[p.first] = new_eta;
}

FeaturePointer Model::tagEntropySimple() const {
  FeaturePointer feat = makeFeaturePointer();
  const size_t taglen = corpus.tags.size();
  double logweights[taglen];
  for(const pair<string, int>& p : corpus.dic) {
    for(size_t t = 0; t < taglen; t++) {
      string feat = "simple-"+p.first+"-"+to_string(t);
      logweights[t] = (*param)[feat];
    }
    logNormalize(logweights, taglen);
    double entropy = 0.0;
    for(size_t t = 0; t < taglen; t++) {
      entropy -= logweights[t] * exp(logweights[t]);
    }
    (*feat)[p.first] = entropy;
  }
  return feat;
}

FeaturePointer Model::wordFrequencies() const {
  FeaturePointer feat = makeFeaturePointer();
  for(const pair<string, int>& p : corpus.dic_counts) {
    (*feat)[p.first] = log(corpus.total_words)-log(p.second);
  }
  return feat;
}

pair<Vector2d, vector<double> > Model::tagBigram() const {
  size_t taglen = corpus.tags.size();
  Vector2d mat = makeVector2d(taglen, taglen, 1.0);
  vector<double> vec(taglen, 1.0);
  for(const Sentence& seq : corpus.seqs) {
    vec[seq.tag[0]]++;
    for(size_t t = 1; t < seq.size(); t++) {
      mat[seq.tag[t-1]][seq.tag[t]]++; 
    }
  }
  for(size_t i = 0; i < taglen; i++) {
    vec[i] = log(vec[i])-log(taglen+corpus.seqs.size());
    double sum_i = 0.0;
    for(size_t j = 0; j < taglen; j++) {
      sum_i += mat[i][j];
    }
    for(size_t j = 0; j < taglen; j++) {
      mat[i][j] = log(mat[i][j])-log(sum_i);
    }
  }
  return make_pair(mat, vec);
}

void Model::run(const Corpus& testCorpus) {
  Corpus retagged(testCorpus);
  retagged.retag(this->corpus); // use training taggs. 
  int testLag = corpus.seqs.size()*testFrequency;
  int numObservation = 0;
  xmllog.begin("param");
  xmllog.begin("Q"); xmllog << Q << endl; xmllog.end();
  xmllog.begin("T"); xmllog << T << endl; xmllog.end();
  xmllog.begin("B"); xmllog << B << endl; xmllog.end();
  xmllog.begin("eta"); xmllog << eta << endl; xmllog.end();
  xmllog.begin("num_train"); xmllog << corpus.size() << endl; xmllog.end();
  xmllog.begin("num_test"); xmllog << testCorpus.size() << endl; xmllog.end();
  xmllog.begin("test_lag"); xmllog << testLag << endl; xmllog.end();
  xmllog.end();
  for(int q = 0; q < Q; q++) {
    xmllog.begin("pass "+to_string(q));
    for(const Sentence& seq : corpus.seqs) {
      xmllog.begin("example_"+to_string(numObservation));
      ParamPointer gradient = this->gradient(seq);
      this->adagrad(gradient);
      xmllog.end();
      numObservation++;
      if(numObservation % testLag == 0) {
	xmllog.begin("test");
	test(retagged);
	xmllog.end();
      }
    }
    xmllog.end();
  }
}

double Model::test(const Corpus& corpus) {
  map<int, int> tagcounts;
  map<int, int> taghits;
  int testcount = 0, alltaghits = 0;
  xmllog.begin("examples");
  for(const Sentence& seq : corpus.seqs) {
    shared_ptr<Tag> tag = this->sample(seq).back();
    xmllog.begin("truth"); xmllog << seq.str() << endl; xmllog.end();
    xmllog.begin("tag"); xmllog << tag->str() << endl; xmllog.end();
    for(int i = 0; i < seq.size(); i++) {
      if(tag->tag[i] == seq.tag[i]) {
	if(taghits.find(tag->tag[i]) == taghits.end())
	  taghits[tag->tag[i]] = 0;
	taghits[tag->tag[i]]++;
	alltaghits++;
      }
      if(tagcounts.find(tag->tag[i]) == tagcounts.end())
	tagcounts[tag->tag[i]] = 0;
      tagcounts[tag->tag[i]]++;
      testcount++;
    }
  }
  xmllog.end();

  xmllog.begin("score"); 
  double f1 = 0.0;
  for(const pair<string, int>& p : corpus.tags) {
    double accuracy = 0;
    if(tagcounts[p.second] != 0)
      accuracy = taghits[p.second]/(double)tagcounts[p.second];
    double recall = 0;
    if((double)corpus.tagcounts.find(p.first)->second != 0)
      recall = taghits[p.second]/(double)corpus.tagcounts.find(p.first)->second;
    xmllog << "<tag: " << p.first << "\taccuracy: " << accuracy << "\trecall: " << recall << "\tF1: " <<
    2*accuracy*recall/(accuracy+recall) << endl;
    // if(accuracy != 0 && recall != 0)
    //  f1 += 2*accuracy*recall/(accuracy+recall);
  }
  double accuracy = (double)alltaghits/testcount;
  xmllog << "test accuracy = " << accuracy*100 << " %" << endl; 
  xmllog.end();
  return f1/corpus.tags.size();
}

void Model::adagrad(ParamPointer gradient) {
  for(const pair<string, double>& p : *gradient) {
    mapUpdate(*G2, p.first, p.second * p.second);
    double this_eta = eta;
    if(stepsize->find(p.first) != stepsize->end()) {
      this_eta = (*stepsize)[p.first];
    }
    mapUpdate(*param, p.first, this_eta * p.second/sqrt(1e-4 + (*G2)[p.first]));
  }
}

ModelTreeUA::ModelTreeUA(const Corpus& corpus, int K) 
:Model(corpus), eps(0), eps_split(0) {
  Model::K = K;
  this->initThreads(K);
}

void ModelTreeUA::run(const Corpus& testCorpus) {   
  ModelSimple simple_model(this->corpus, T, B, Q, eta);
  simple_model.run(testCorpus, true);
  copyParamFeatures(simple_model.param, "simple-", this->param, "");
  Model::run(testCorpus); 
}


void ModelTreeUA::workerThreads(int tid, int seed, shared_ptr<MarkovTreeNode> node, Tag tag, objcokus rng) {
    while(true) {
      tag.rng = &rng; // unsafe, rng may be deleted.
      node->gradient = tag.proposeGibbs(rng.randomMT() % tag.size(), true);
     // xmllog.begin("tag"); xmllog << "[" << node->depth << "] " 
      //			<< tag.str() << endl; xmllog.end();
      this->configStepsize(node->gradient, this->eta);
      if(node->depth < B) node->log_weight = -DBL_MAX;
      else node->log_weight = this->score(tag); 

      if(node->depth == 0) { // multithread split.
	unique_lock<mutex> lock(th_mutex);
	active_work--;
	vector<objcokus> cokus(K);
	for(int k = 0; k < K; k++) {
	  int new_seed = getFingerPrint((k+5)*3, seed);
	  cokus[k].seedMT(new_seed);
	  node->children.push_back(makeMarkovTreeNode(node));
	  th_work.push_back(make_tuple(new_seed, node->children.back(), tag, cokus[k]));
	}
	th_cv.notify_all();
	lock.unlock();
	return;
      }else if(log(rng.random01()) < log(this->eps_split)) {
	for(int k = 0; k < K; k++) {
	  node->children.push_back(makeMarkovTreeNode(node));
	  workerThreads(tid, seed, node->children.back(), tag, rng);
	}
	return;
      }else if(node->depth >= B && log(rng.random01()) < log(this->eps)) { // stop.
	unique_lock<mutex> lock(th_mutex);
	xmllog.begin("tag");  xmllog << tag.str() << endl; xmllog.end();
	xmllog.begin("weight"); xmllog << node->log_weight << endl; xmllog.end();
	xmllog.begin("time"); xmllog << node->depth << endl; xmllog.end();
	active_work--;
	lock.unlock();
	return;
      }else{                             // proceed as chain.
	node->children.push_back(makeMarkovTreeNode(node));
	node = node->children.back();
      }
    }
}

void ModelTreeUA::initThreads(size_t numThreads) {
  th.resize(numThreads);
  active_work = 0;
  for(size_t ni = 0; ni < numThreads; ni++) {
    this->th_stream.push_back(shared_ptr<stringstream>(new stringstream()));
    this->th_log.push_back(shared_ptr<XMLlog>(new XMLlog(*th_stream.back())));
    this->th[ni] = shared_ptr<thread>(new thread([&] (int tid) {
      unique_lock<mutex> lock(th_mutex);
      while(true) {
	if(th_work.size() > 0) {
	  tuple<int, shared_ptr<MarkovTreeNode>, Tag, objcokus> work = th_work.front();
	  th_work.pop_front();
	  active_work++;
	  lock.unlock();
	  th_stream[tid]->str("");
	  workerThreads(tid, get<0>(work), get<1>(work), get<2>(work), get<3>(work));
	  th_finished.notify_all();
	  lock.lock();
	}else{
	  th_cv.wait(lock);	
	}
      }
    }, ni));
  }
}

shared_ptr<MarkovTree> ModelTreeUA::explore(const Sentence& seq) {
  this->eps = 1.0/(T-B);
  shared_ptr<MarkovTree> tree = shared_ptr<MarkovTree>(new MarkovTree());
  xmllog.begin("truth"); xmllog << seq.str() << endl; xmllog.end();
  Tag tag(&seq, corpus, &rngs[0], param);
  objcokus cokus; // cokus is not re-entrant.
  cokus.seedMT(0);
  unique_lock<mutex> lock(th_mutex);
  th_work.push_back(make_tuple(0, tree->root, tag, cokus));
  th_cv.notify_one();
  while(true) {
    th_finished.wait(lock);
    if(active_work + th_work.size() == 0) break;
  }
  lock.unlock();
  for(int k = 0; k < K; k++) {
    xmllog.begin("thread_"+to_string(k));
    xmllog.logRaw(th_stream[k]->str());
    xmllog << endl;
    xmllog.end();
  }
  return tree;
}

ParamPointer ModelTreeUA::gradient(const Sentence& seq) {
  return explore(seq)->expectedGradient();
}

TagVector ModelTreeUA::sample(const Sentence& seq) {
  return explore(seq)->getSamples();
}
double ModelTreeUA::score(const Tag& tag) {
  const Sentence* seq = tag.seq;
  double score = 0.0;
  for(int i = 0; i < tag.size(); i++) {
    score -= (tag.tag[i] != seq->tag[i]);
  }
  return score;
}


ModelAdaTree::ModelAdaTree(const Corpus& corpus, int K, double c, double Tstar)
:ModelTreeUA(corpus, K), m_c(c), m_Tstar(Tstar) {
  // aggregate stats. 
  wordent = tagEntropySimple();
  wordfreq = wordFrequencies();
  auto tag_bigram_unigram = tagBigram();
  tag_bigram = tag_bigram_unigram.first;
  tag_unigram_start = tag_bigram_unigram.second;
  this->etaT = this->eta;
}

void ModelAdaTree::workerThreads(int tid, int seed, shared_ptr<MarkovTreeNode> node, Tag tag, objcokus rng) { 
    XMLlog& lg = *th_log[tid];
    while(true) {
      tag.rng = &rng; // unsafe, rng may be deleted. 
      node->gradient = tag.proposeGibbs(rng.randomMT() % tag.size(), true);
      node->tag = shared_ptr<Tag>(new Tag(tag));
      auto predT = this->logisticStop(node, *tag.seq, tag); 
      double prob = get<0>(predT);
      node->posgrad = get<1>(predT);
      node->neggrad = get<2>(predT);
      ParamPointer feat = get<3>(predT); 
      th_mutex.lock();
      this->configStepsize(node->gradient, this->eta);
      this->configStepsize(feat, this->etaT);
      th_mutex.unlock();

      lg.begin("tag"); 
      lg << "[seed: " << seed << "] " 
	  << "[thread: " << tid << "] "
	  << "[depth: " << node->depth << "] " 
	  << "[prob: " << prob << "] "
	  << tag.str() << endl;
      lg.end();
      
      if(node->depth < B) node->log_weight = -DBL_MAX;
      else node->log_weight = this->score(node, tag)+log(prob); 

      if(node->depth == 0) { // multithread split.
	unique_lock<mutex> lock(th_mutex);
	active_work--;
	vector<objcokus> cokus(K);
	for(int k = 0; k < K; k++) {
	  int new_seed = getFingerPrint((k+5)*3, seed);
	  cokus[k].seedMT(new_seed);
	  node->children.push_back(makeMarkovTreeNode(node));
	  th_work.push_back(make_tuple(new_seed, node->children.back(), tag, cokus[k]));
	}
	th_cv.notify_all();
	lock.unlock();
	return;
      }else if(log(rng.random01()) < log(this->eps_split)) {
	for(int k = 0; k < K; k++) {
	  node->children.push_back(makeMarkovTreeNode(node));
	  workerThreads(tid, seed, node->children.back(), tag, rng);
	}
	return;
      }else if(node->depth >= B && log(rng.random01()) < log(prob)) { // stop.
	unique_lock<mutex> lock(th_mutex);
	lg.begin("final-tag");  lg << tag.str() << endl; lg.end();
	lg.begin("weight"); lg << node->log_weight << endl; lg.end();
	lg.begin("time"); lg << node->depth << endl; lg.end();
	lg.begin("feat");
	for(const pair<string, double>& p : *feat) {
	  lg << p.first << " : " << p.second 
		 << " , param : " << (*param)[p.first] << endl;
	}
	lg.end();
	active_work--;
	lock.unlock();
	return;
      }else{                             // proceed as chain.
	node->children.push_back(makeMarkovTreeNode(node));
	node = node->children.back();
      }
    }
}

FeaturePointer ModelAdaTree::extractStopFeatures(shared_ptr<MarkovTreeNode> node, const Sentence& seq, const Tag& tag) {
  FeaturePointer feat = makeFeaturePointer();
  size_t seqlen = tag.size();
  size_t taglen = corpus.tags.size();
  // feat: bias.
  (*feat)["bias-stopornot"] = 1.0;
  // feat: word len.
  (*feat)["len-stopornot"] = (double)seqlen; 
  (*feat)["len-inv-stopornot"] = 1/(double)seqlen;
  // feat: entropy and frequency.
  for(size_t t = 0; t < seqlen; t++) {
    string word = seq.seq[t].word;
    if(wordent->find(word) == wordent->end())
      (*feat)["ent-"+word] = log(taglen); // no word, use maxent.
    else
      (*feat)["ent-"+word] = (*wordent)[word];
    if(wordfreq->find(word) == wordfreq->end())
      (*feat)["freq-"+word] = log(corpus.total_words);
    else
      (*feat)["freq-"+word] = (*wordfreq)[word];
  }
  // feat: avg sample path length.
  int L = 3, l = 0;
  double dist = 0.0;
  shared_ptr<MarkovTreeNode> p = node;
  while(p->depth >= 1 && L >= 0) {
    auto pf = p->parent.lock();
    if(pf == nullptr) throw "MarkovTreeNode father is expired.";
    dist += p->tag->distance(*pf->tag);
    l++; L--;
    p = pf;
  }
  if(l > 0) dist /= l;
  (*feat)["len-sample-path"] = dist;
  // log probability of current sample in terms of marginal training stats.
  double logprob = tag_unigram_start[tag.tag[0]];
  for(size_t t = 1; t < seqlen; t++) 
    logprob += tag_bigram[tag.tag[t-1]][tag.tag[t]];
  (*feat)["log-prob-tag-bigram"] = logprob;
  return feat;
}

tuple<double, ParamPointer, ParamPointer, ParamPointer> 
ModelAdaTree::logisticStop(shared_ptr<MarkovTreeNode> node, const Sentence& seq, const Tag& tag) {
  ParamPointer posgrad = makeParamPointer(), 
		neggrad = makeParamPointer();
  FeaturePointer feat = this->extractStopFeatures(node, seq, tag);
  double prob = logisticFunc(log(eps)-log(1-eps)+tag.score(feat)); 
  if(isnan(prob)) {
    th_mutex.lock();
    for(const pair<string, double>& p : *feat) {
      xmllog << p.first << " : " << (*param)[p.first] << endl;
    }
    th_mutex.unlock();
  }
  if(prob < 1e-3) prob = 1e-3;   // truncation, avoid too long transitions.
  else{
    mapUpdate<double, double>(*posgrad, *feat, (1-prob));
    mapUpdate<double, double>(*neggrad, *feat, -prob);
  }
  return make_tuple(prob, posgrad, neggrad, feat);
}

double ModelAdaTree::score(shared_ptr<MarkovTreeNode> node, const Tag& tag) {
  const Sentence* seq = tag.seq;
  double score = 0.0;
  for(int i = 0; i < tag.size(); i++) {
    score -= (tag.tag[i] != seq->tag[i]);
  }
  score -= m_c * max(0.0, node->depth-m_Tstar);
  return score;
}
