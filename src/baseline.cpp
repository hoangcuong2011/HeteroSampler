/* implementation of baseline sequence tagging models, including
 *  > independent logistic regression.
 *  > CRF with Gibbs sampling.
 */
#include "model.h"
#include "feature.h"
#include <boost/program_options.hpp>

using namespace std;
using namespace std::placeholders;
namespace po = boost::program_options;

namespace HeteroSampler {

  ////////// Simple Model (Independent Logistic Regression) ////////////
  ModelSimple::ModelSimple(ptr<Corpus> corpus, const po::variables_map& vm)
  :Model(corpus, vm) {
    this->depthL = vm["depthL"].empty() ? 0 : vm["depthL"].as<int>();
    this->windowL = vm["windowL"].empty() ? 0 : vm["windowL"].as<int>();

    xmllog->begin("windowL");
    (*xmllog) << windowL << endl;
    xmllog->end();
  }

  void ModelSimple::sample(Tag& tag, int time, bool argmax) {
    for(int t = 0; t < time; t++) {
      for(size_t i = 0; i < tag.size(); i++) {
        auto featExtract = [&] (const Tag& tag) -> FeaturePointer {
                              return this->extractFeatures(tag, i);
                            };
        tag.proposeGibbs(i, featExtract, false, false, argmax);
      }
    }
  }

  TagVector ModelSimple::sample(const Instance& seq, bool argmax) {
    assert(argmax == true);
    TagVector vec;
    gradient(seq, &vec, false);
    return vec;
  }

  FeaturePointer ModelSimple::extractFeatures(const Tag& tag, int pos) {
    FeaturePointer features = makeFeaturePointer();
    const vector<TokenPtr>& sen = tag.seq->seq;
    int seqlen = tag.size();
    // extract word features only.
    for(int l = max(0, pos - windowL); l <= min(pos + windowL, seqlen-1); l++) {
      StringVector nlp = NLPfunc(cast<TokenLiteral>(sen[l])->word);
      for(const string& token : *nlp) {
        stringstream ss;
        ss << "simple-w-" << to_string(l-pos)
           << "-" << token << "-" << corpus->invtag(tag.tag[pos]);
        insertFeature(features, ss.str());
      }
    }
    return features;
  }

  ParamPointer ModelSimple::gradient(const Instance& seq) {
    return this->gradient(seq, nullptr, true);
  }

  ParamPointer ModelSimple::gradient(const Instance& seq, TagVector* samples, bool update_grad) {
    Tag tag(&seq, corpus, &rngs[0], param);
    Tag truth(seq, corpus, &rngs[0], param);
    ParamPointer gradient = makeParamPointer();
    for(size_t i = 0; i < tag.size(); i++) {
      auto featExtract = [&] (const Tag& tag) -> FeaturePointer {
                            return this->extractFeatures(tag, i);
                          };
      ParamPointer g = tag.proposeGibbs(i, featExtract, true, false);
      if(update_grad) {
        mapUpdate<double, double>(*gradient, *g);
        mapUpdate<double, double>(*gradient, *featExtract(truth));
      }
    }
    if(samples)
      samples->push_back(shared_ptr<Tag>(new Tag(tag)));
    else{
      xmllog->begin("truth"); (*xmllog) << seq.str() << endl; xmllog->end();
      xmllog->begin("tag"); (*xmllog) << tag.str() << endl; xmllog->end();
    }
    return gradient;
  }

  void ModelSimple::saveMetaData(ostream& os) const {
    Model::saveMetaData(os);
    os << "depthL " << boost::lexical_cast<string>(this->depthL) << endl;
    os << "windowL " << boost::lexical_cast<string>(this->windowL) << endl;
    os << endl;
  }

  void ModelSimple::loadMetaData(istream& is) {
    Model::loadMetaData(is);
    string line;
    while(!is.eof()) {
      getline(is, line);
      if(line == "") break;
      vector<string> parts;
      split(parts, line, boost::is_any_of(" "));
      if(parts[0] == "depthL") {
        this->depthL = boost::lexical_cast<int>(parts[1]);
      }else if(parts[0] == "windowL") {
        this->windowL = boost::lexical_cast<int>(parts[1]);
      }
    }
  }

  void ModelSimple::logArgs() {
    Model::logArgs();
    xmllog->begin("windowL"); (*xmllog) << windowL << endl; xmllog->end();
    xmllog->begin("depthL"); (*xmllog) << depthL << endl; xmllog->end();
  }

  //////// Model CRF Gibbs ///////////////////////////////
  ModelCRFGibbs::ModelCRFGibbs(ptr<Corpus> corpus, const po::variables_map& vm)
   :ModelSimple(corpus, vm) {

    // lambda expression for feature extractions.
    this->extractFeatures = [] (ptr<Model> model, const GraphicalModel& gm, int pos) {

      assert(isinstance<ModelCRFGibbs>(model));
      ptr<ModelCRFGibbs> this_model = cast<ModelCRFGibbs>(model);
      const Tag& tag = dynamic_cast<const Tag&>(gm);
      assert(isinstance<CorpusLiteral>(tag.corpus));
      const vector<TokenPtr>& sen = tag.seq->seq;
      int seqlen = tag.size();

      // extract word features.
      FeaturePointer features = makeFeaturePointer();
      extractUnigramFeature(tag, pos, this_model->windowL, this_model->depthL, features);

      // extract higher-order grams.
      for(int factor = 1; factor <= this_model->factorL; factor++) {
       for(int p = pos; p < pos+factor; p++) {
         if(p-factor+1 >= 0 && p < seqlen) {
           extractXgramFeature(tag, p, factor, features);
         }
       }
      }

      return features;
    };

    this->extractFeatAll = [] (ptr<Model> model, const GraphicalModel& gm) {

      assert(isinstance<ModelCRFGibbs>(model));
      ptr<ModelCRFGibbs> this_model = cast<ModelCRFGibbs>(model);
      auto& tag = dynamic_cast<const Tag&>(gm);
      assert(isinstance<CorpusLiteral>(tag.corpus));
      const vector<TokenPtr>& sen = tag.seq->seq;
      int seqlen = tag.size();

      // extract word features.
      FeaturePointer features = makeFeaturePointer();

      for(int pos = 0; pos < seqlen; pos++) {
        // extract unigram features.
        extractUnigramFeature(tag, pos, this_model->windowL, this_model->depthL, features);
        // extract higher-order grams.
        for(int factor = 1; factor <= this_model->factorL; factor++) {
           if(pos-factor+1 >= 0) {
             extractXgramFeature(tag, pos, factor, features);
           }
         }
      }

      return features;
    };

    getMarkovBlanket = [] (ptr<Model> model, const GraphicalModel& gm, int pos) {

      assert(isinstance<ModelCRFGibbs>(model));
      ptr<ModelCRFGibbs> this_model = cast<ModelCRFGibbs>(model);

      vec<int> ret;
      for(int p = fmax(0, pos - this_model->factorL + 1); p <= fmin(pos + this_model->factorL -1, gm.size()-1); p++) {
        if(p == pos) continue;
        ret.push_back(p);
      }
      return ret;
    };

    getInvMarkovBlanket = getMarkovBlanket; // markov network.

    this->factorL = vm["factorL"].empty() ? 2 : vm["factorL"].as<int>();
    this->annealing = vm["temp"].empty() ? "" : vm["temp"].as<string>();

    if(isinstance<CorpusLiteral>(corpus))
      cast<CorpusLiteral>(corpus)->computeWordFeat();

    if(annealing == "scanline") { // use the annealing scheme introduced in scanline paper (CVPR 2014).
      // TODO: implement CRF Gibbs samplers with annealing.
      throw "simulated annealing is not supported by CRF Gibbs samplers";
      // temp_decay = vm["temp_decay"].as<double>();
      // temp_magnify = vm["temp_magnify"].as<double>();
      // temp_init = vm["temp_init"].as<double>();
    }

    this->time = 0;
   }

  void ModelCRFGibbs::sample(Tag& tag, int time, bool argmax) {
    for(int t = 0; t < time; t++) {
      this->sampleOneSweep(tag, argmax);
    }
  }

  ParamPointer ModelCRFGibbs::
  proposeGibbs(Tag& tag, objcokus& rng, int pos, FeatureExtractOne feat_extract,
               bool grad_expect, bool grad_sample, bool use_meta_feature) {
    int seqlen = tag.size();
    if(pos >= seqlen)
      throw "Gibbs sampling proposal out of bound.";
    int taglen = corpus->tags.size();

    // Enumerative Gibbs sampling.
    int oldval = tag.tag[pos];
    if(use_meta_feature) {
      tag.prev_sc[pos] = tag.this_sc[pos];
      tag.oldlabels[pos] = oldval;
      tag.oldval = oldval;
    }

    /* compute score */
    vector<FeaturePointer> featvec;
    vector<double> sc(taglen);
    auto computeSc = [&] (int i) {
      int backup = tag.tag[i];
      for(int t = 0; t < taglen; t++) {
        tag.tag[i] = t;
        FeaturePointer features = feat_extract(shared_from_this(), tag, i);
        featvec.push_back(features);
        sc[t] = HeteroSampler::score(this->param, features);
      }
      tag.tag[i] = backup;
    };


    computeSc(pos);
    tag.sc = sc;
    logNormalize(&sc[0], taglen);
    logNormalize(&tag.sc[0], taglen);

    int val;
    val = rng.sampleCategorical(&sc[0], taglen);
    if(val == taglen) throw "Gibbs sample out of bound.";
    tag.tag[pos] = val;

    // compute statistics.
    tag.reward[pos] = (tag.sc[val] - tag.sc[oldval]);
    if(use_meta_feature) {
      tag.this_sc[pos] = tag.sc;
      tag.prev_entropy[pos] = tag.entropy[pos];
      tag.entropy[pos] = logEntropy(&tag.sc[0], taglen);
      tag.timestamp[pos] += 1;
      this->time += 1;
    }

    // compute gradient, if necessary.
    tag.features = feat_extract(shared_from_this(), tag, pos);
    ParamPointer gradient = makeParamPointer();
    if(grad_sample)
      mapUpdate<double, double>(*gradient, *tag.features);
    if(grad_expect) {
      for(int t = 0; t < taglen; t++) {
        mapUpdate<double, double>(*gradient, *featvec[t], -exp(tag.sc[t]));
      }
    }
    return gradient;
  }

  ptr<GraphicalModel> ModelCRFGibbs::makeSample(const Instance& instance, ptr<Corpus> corpus, objcokus* rng) const {
    return std::make_shared<Tag>(&instance, corpus, rng, param);
  }

  ptr<GraphicalModel> ModelCRFGibbs::makeTruth(const Instance& instance, ptr<Corpus> corpus, objcokus* rng) const {
    return std::make_shared<Tag>(instance, corpus, rng, param);
  }

  ptr<GraphicalModel> ModelCRFGibbs::copySample(const GraphicalModel& gm) const {
    auto& tag = dynamic_cast<const Tag&>(gm);
    return make_shared<Tag>(tag);
  }

  void ModelCRFGibbs::sampleOne(GraphicalModel& gm, objcokus& rng, int choice, FeatureExtractOne feat_extract, bool use_meta_feature) {
    Tag& tag = dynamic_cast<Tag&>(gm);
    if(choice >= tag.size())
      throw "kernel choice invalid (>= tag size)";
    this->proposeGibbs(tag, rng, choice, feat_extract, false, false, use_meta_feature);
  }

  void ModelCRFGibbs::sampleOneAtInit(GraphicalModel& gm, objcokus& rng, int choice, bool use_meta_feature) {
    this->sampleOne(gm, rng, choice, this->extractFeaturesAtInit, use_meta_feature);
  }

  void ModelCRFGibbs::sampleOne(GraphicalModel& gm, objcokus& rng, int choice, bool use_meta_feature) {
    this->sampleOne(gm, rng, choice, this->extractFeatures, use_meta_feature);
  }

  TagVector ModelCRFGibbs::sample(const Instance& seq, bool argmax) {
    TagVector vec;
    TagPtr tag = makeTagPtr(&seq, corpus, &rngs[0], param);
    for(int t = 0; t < T; t++) {
      this->sample(*tag, 1, argmax);
      if(t < B) continue;
      vec.push_back(tag);
      tag = TagPtr(new Tag(*tag));
    }
    return vec;
  }

  double ModelCRFGibbs::score(const GraphicalModel& gm) {
    auto& tag = dynamic_cast<const Tag&>(gm);
    FeaturePointer feat = this->extractFeaturesAll(tag);
    return HeteroSampler::score(this->param, feat);
  }

  FeaturePointer ModelCRFGibbs::extractFeaturesAll(const Tag& tag) {
    return extractFeatAll(shared_from_this(), tag);
  }

  void ModelCRFGibbs::sampleOneSweep(Tag& tag, bool argmax) {
    for(int i = 0; i < tag.tag.size(); i++) {
      tag.proposeGibbs(i, [&] (const Tag& tag) -> FeaturePointer {
                            return this->extractFeatures(shared_from_this(), tag, i);
                          }, false, false, argmax);
    }
  }

  void ModelCRFGibbs::saveMetaData(ostream& os) const {
    ModelSimple::saveMetaData(os);
    os << "factorL " << boost::lexical_cast<string>(this->factorL) << endl;
    os << endl;
  }

  void ModelCRFGibbs::loadMetaData(istream& is) {
    ModelSimple::loadMetaData(is);
    string line;
    while(!is.eof()) {
      getline(is, line);
      if(line == "") break;
      vector<string> parts;
      split(parts, line, boost::is_any_of(" "));
      if(parts[0] == "factorL") {
        this->factorL = boost::lexical_cast<int>(parts[1]);
      }
    }
  }


  void ModelCRFGibbs::logArgs() {
    ModelSimple::logArgs();
    xmllog->begin("factorL"); (*xmllog) << factorL << endl; xmllog->end();
  }

  ParamPointer ModelCRFGibbs::gradient(const Instance& seq) {
    return this->gradient(seq, nullptr, true);
  }

  ParamPointer ModelCRFGibbs::gradient(const Instance& seq, TagVector* samples, bool update_grad) {
    Tag tag(&seq, corpus, &rngs[0], param);
    Tag truth(seq, corpus, &rngs[0], param);
    ParamPointer gradient = makeParamPointer();
    for(int t = 0; t < T; t++) {
      this->sampleOneSweep(tag);
      if(t < B) continue;
      if(update_grad)
        mapUpdate<double, double>(*gradient, *this->extractFeaturesAll(tag));
    }
    if(samples)
      samples->push_back(shared_ptr<Tag>(new Tag(tag)));
    // xmllog->begin("truth"); (*xmllog) << seq.str() << endl; xmllog->end();
    // xmllog->begin("tag"); (*xmllog) << tag.str() << endl; xmllog->end();
    if(update_grad) {
      FeaturePointer feat = this->extractFeaturesAll(truth);
      mapDivide<double>(*gradient, -(double)(T-B));
      mapUpdate<double, double>(*gradient, *feat);
    }
    return gradient;
  }
}
