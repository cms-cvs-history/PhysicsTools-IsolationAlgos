#include "PhysicsTools/IsolationAlgos/plugins/CandIsolatorFromDeposits.h"

// Framework
#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "DataFormats/Common/interface/Handle.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/Framework/interface/ESHandle.h"

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/MuonReco/interface/Muon.h"
#include "DataFormats/RecoCandidate/interface/IsoDepositDirection.h"
#include "DataFormats/RecoCandidate/interface/IsoDeposit.h"
#include "DataFormats/RecoCandidate/interface/IsoDepositFwd.h"
#include "DataFormats/RecoCandidate/interface/IsoDepositVetos.h"

#include "DataFormats/Candidate/interface/CandAssociation.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include <string>
#include <boost/regex.hpp>

using namespace edm;
using namespace std;
using namespace reco;
using namespace reco::isodeposit;

bool isNumber(const std::string &str) {
   static boost::regex re("^[+-]?(\\d+\\.?|\\d*\\.\\d*)$");
   return regex_match(str.c_str(), re);
}
double toNumber(const std::string &str) {
    return atof(str.c_str());
}

CandIsolatorFromDeposits::SingleDeposit::SingleDeposit(const edm::ParameterSet &iConfig) :
  src_(iConfig.getParameter<edm::InputTag>("src")),
  deltaR_(iConfig.getParameter<double>("deltaR")),
  weightExpr_(iConfig.getParameter<std::string>("weight")),
  skipDefaultVeto_(iConfig.getParameter<bool>("skipDefaultVeto"))
						      //,vetos_(new AbsVetos())
{
  std::string mode = iConfig.getParameter<std::string>("mode");
  if (mode == "sum") mode_ = Sum; 
  else if (mode == "sumRelative") mode_ = SumRelative; 
  //else if (mode == "max") mode_ = Max;                  // TODO: on request only
  //else if (mode == "maxRelative") mode_ = MaxRelative;  // TODO: on request only
  else if (mode == "count") mode_ = Count;
  else throw cms::Exception("Not Implemented") << "Mode '" << mode << "' not implemented. " <<
    "Supported modes are 'sum', 'sumRelative', 'count'." << 
    //"Supported modes are 'sum', 'sumRelative', 'max', 'maxRelative', 'count'." << // TODO: on request only
    "New methods can be easily implemented if requested.";
  typedef std::vector<std::string> vstring;
  vstring vetos = iConfig.getParameter< vstring >("vetos");
  for (vstring::const_iterator it = vetos.begin(), ed = vetos.end(); it != ed; ++it) {
    if (!isNumber(*it)) {
      static boost::regex threshold("Threshold\\((\\d+\\.\\d+)\\)"), 
	cone("ConeVeto\\((\\d+\\.\\d+)\\)"),
	angleCone("AngleCone\\((\\d+\\.\\d+)\\)"),
	angleVeto("AngleVeto\\((\\d+\\.\\d+)\\)");
      boost::cmatch match;
      if (regex_match(it->c_str(), match, threshold)) {
	vetos_.push_back(new ThresholdVeto(toNumber(match[1].first)));
      } else if (regex_match(it->c_str(), match, cone)) {
	vetos_.push_back(new ConeVeto(reco::isodeposit::Direction(), toNumber(match[1].first)));
      } else if (regex_match(it->c_str(), match, angleCone)) {
	vetos_.push_back(new AngleCone(reco::isodeposit::Direction(), toNumber(match[1].first)));
      } else if (regex_match(it->c_str(), match, angleVeto)) {
	vetos_.push_back(new AngleConeVeto(reco::isodeposit::Direction(), toNumber(match[1].first)));
      } else {
	throw cms::Exception("Not Implemented") << "Veto " << it->c_str() << " not implemented yet...";
      }
    }  else {
      //std::cout << "Adding veto of radius " << toNumber(*it) << std::endl;
      vetos_.push_back(new ConeVeto(reco::isodeposit::Direction(), toNumber(*it)));
    }
  }
  std::string weight = iConfig.getParameter<std::string>("weight");
  if (isNumber(weight)) {
    //std::cout << "Weight is a simple number, " << toNumber(weight) << std::endl;
    weight_ = toNumber(weight);
    usesFunction_ = false;
  } else {
    usesFunction_ = true;
    //std::cout << "Weight is a function, this might slow you down... " << std::endl;
  }
  std::cout << "Total of " << vetos_.size() << " vetos" << std::endl;
}
void CandIsolatorFromDeposits::SingleDeposit::cleanup() {
    for (AbsVetos::iterator it = vetos_.begin(), ed = vetos_.end(); it != ed; ++it) {
        delete *it;
    }
}
void CandIsolatorFromDeposits::SingleDeposit::open(const edm::Event &iEvent) {
    iEvent.getByLabel(src_, hDeps_);
}

double CandIsolatorFromDeposits::SingleDeposit::compute(const reco::CandidateBaseRef &cand) {
    const IsoDeposit &dep = (*hDeps_)[cand];
    double eta = dep.eta(), phi = dep.phi(); // better to center on the deposit direction
                                             // that could be, e.g., the impact point at calo
    for (AbsVetos::iterator it = vetos_.begin(), ed = vetos_.end(); it != ed; ++it) {
        (*it)->centerOn(eta, phi);
    }
    double weight = (usesFunction_ ? weightExpr_(*cand) : weight_);
    switch (mode_) {
        case Sum: return weight * dep.depositWithin(deltaR_, vetos_, skipDefaultVeto_);
        case SumRelative: return weight * dep.depositWithin(deltaR_, vetos_, skipDefaultVeto_) / dep.candEnergy() ;
        case Count: return weight * dep.depositAndCountWithin(deltaR_, vetos_, skipDefaultVeto_).second ;
    }
    throw cms::Exception("Logic error") << "Should not happen at " << __FILE__ << ", line " << __LINE__; // avoid gcc warning
}


/// constructor with config
CandIsolatorFromDeposits::CandIsolatorFromDeposits(const ParameterSet& par) {
  typedef std::vector<edm::ParameterSet> VPSet;
  VPSet depPSets = par.getParameter<VPSet>("deposits");
  for (VPSet::const_iterator it = depPSets.begin(), ed = depPSets.end(); it != ed; ++it) {
    sources_.push_back(SingleDeposit(*it));
  }
  if (sources_.size() == 0) throw cms::Exception("Configuration Error") << "Please specify at least one deposit!";
  produces<CandDoubleMap>();
}

/// destructor
CandIsolatorFromDeposits::~CandIsolatorFromDeposits() {
  vector<SingleDeposit>::iterator it, begin = sources_.begin(), end = sources_.end();
  for (it = begin; it != end; ++it) it->cleanup();
}

/// build deposits
void CandIsolatorFromDeposits::produce(Event& event, const EventSetup& eventSetup){

  vector<SingleDeposit>::iterator it, begin = sources_.begin(), end = sources_.end();
  for (it = begin; it != end; ++it) it->open(event);

  const IsoDepositMap & map = begin->map();

  if (map.size()==0) { // !!???
        event.put(auto_ptr<CandDoubleMap>(new CandDoubleMap()));
        return;
  }
  auto_ptr<CandDoubleMap> ret(new CandDoubleMap());
  CandDoubleMap::Filler filler(*ret);

  typedef reco::IsoDepositMap::const_iterator iterator_i; 
  typedef reco::IsoDepositMap::container::const_iterator iterator_ii; 
  iterator_i depI = map.begin(); 
  iterator_i depIEnd = map.end(); 
  for (; depI != depIEnd; ++depI){ 
    std::vector<double> retV(depI.size(),0);
    edm::Handle<edm::View<reco::Candidate> > candH;
    event.get(depI.id(), candH);
    const edm::View<reco::Candidate>& candV = *candH;

    iterator_ii depII = depI.begin(); 
    iterator_ii depIIEnd = depI.end(); 
    size_t iRet = 0;
    for (; depII != depIIEnd; ++depII,++iRet){ 
      double sum=0;
      for (it = begin; it != end; ++it) sum += it->compute(candV.refAt(iRet)); 
      retV[iRet] = sum;
    }
    filler.insert(candH, retV.begin(), retV.end());
  }
  filler.fill();
  event.put(ret);
}

DEFINE_FWK_MODULE( CandIsolatorFromDeposits );
