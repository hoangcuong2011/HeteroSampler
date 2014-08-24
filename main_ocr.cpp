#include "corpus.h"
#include "objcokus.h"
#include "tag.h"
#include "model.h"
#include "utils.h"
#include <iostream>
#include <boost/program_options.hpp>

using namespace std;
using namespace Tagging;

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
  /* parse args. */
  const int T = 10, B = 0, Q = 10, 
	    Q0 = 1, K = 5;  
  const double eta = 0.4;    // default args.
  po::options_description desc("Allowed options");
  desc.add_options()
      ("help", "produce help message")
      ("inference", po::value<string>(), "inference method (Gibbs, TreeUA)")
      ("eta", po::value<double>()->default_value(eta), "step size")
      ("etaT", po::value<double>()->default_value(eta), "step size for time adaptation")
      ("T", po::value<size_t>()->default_value(T), "number of transitions")
      ("B", po::value<size_t>()->default_value(B), "number of burnin steps")
      ("Q", po::value<size_t>()->default_value(Q), "number of passes")
      ("Q0", po::value<int>()->default_value(Q0), "number of passes for smart init")
      ("K", po::value<size_t>()->default_value(K), "number of threads/particles")
      ("c", po::value<double>()->default_value(0), "extent of time regularization")
      ("windowL", po::value<int>()->default_value(0), "window size for node-wise features")
      ("depthL", po::value<int>()->default_value(0), "depth size for node-wise features")
      ("factorL", po::value<int>()->default_value(2), "up to what order of gram should be used")
      ("Tstar", po::value<double>()->default_value(T), "time resource constraints")
      ("eps_split", po::value<double>()->default_value(0.0), "prob of split in MarkovTree")
      ("scoring", po::value<string>()->default_value("Acc"), "scoring (Acc, NER)")
      ("train", po::value<string>(), "training data")
      ("test", po::value<string>(), "test data")
      ("testFrequency", po::value<double>()->default_value(0.3), "frequency of testing")
      ("output", po::value<string>()->default_value("model/default.model"), "output model file")
  ;
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);    
  if(vm.count("help")) {
      cout << desc << "\n";
      return 1;
  }
  string inference = "Gibbs";
  if(vm.count("inference")) {
    inference = vm["inference"].as<string>();
  }
  try{
    /* load corpus. */
    string train = "data/eng_ner/train", test = "data/eng_ner/test";
    if(vm.count("train")) train = vm["train"].as<string>();
    if(vm.count("test")) test = vm["test"].as<string>();  
    Corpus corpus;
    corpus.read<SentenceOCR<16, 8> >(train);
    Corpus testCorpus;
    testCorpus.read<SentenceOCR<16, 8> >(test);

    /* run. */
    corpus.computeWordFeat();
    string output = vm["output"].as<string>();
    size_t pos = output.find_last_of("/");
    if(pos == string::npos) throw "invalid model output dir."; 
    system(("mkdir -p "+output.substr(0, pos)).c_str());
    shared_ptr<Model> model = nullptr;
    if(inference == "Gibbs") {
      model = shared_ptr<ModelCRFGibbs>(new ModelCRFGibbs(&corpus, vm));
      model->run(testCorpus);
    }else if(inference == "Simple") {
      model = shared_ptr<Model>(new ModelSimple(&corpus, vm));
      model->run(testCorpus);
    }else if(inference == "TreeUA") {
      model = shared_ptr<ModelTreeUA>(new ModelTreeUA(&corpus, vm));
      model->run(testCorpus);
    }else if(inference == "AdaTree") {
      model = shared_ptr<ModelAdaTree>(new ModelAdaTree(&corpus, vm));
      model->run(testCorpus);
    }else if(inference == "GibbsIncr") { 
      model = shared_ptr<Model>(new ModelIncrGibbs(&corpus, vm));
      model->run(testCorpus);
    }
    ofstream file;
    file.open(vm["output"].as<string>());
    file << *model;
    file.close();
  }catch(char const* exception) {
    cerr << "Exception: " << string(exception) << endl;
  }
}