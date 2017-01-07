/**
 *  @copyright Copyright 2016 The J-PET Framework Authors. All rights reserved.
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may find a copy of the License in the LICENCE file.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *  @file TaskB1.cpp
 */

#include <map>
#include <string>
#include <vector>
#include <JPetWriter/JPetWriter.h>
#include "Module2.h"

using namespace std;

Module2::Module2(const char * name, const char * description) : JPetTask(name, description){}

Module2::~Module2(){}

//Module init method
void Module2::init(const JPetTaskInterface::Options&){
	INFO("Module 2 started.");
	fBarrelMap.buildMappings(getParamBank());
	getStatistics().createHistogram(new TH1F("remainig_leading_sig_ch_per_thr", "Remainig Leading Signal Channels", 4, 0.5, 4.5));
	getStatistics().createHistogram(new TH1F("remainig_trailing_sig_ch_per_thr", "Remainig Trailing Signal Channels", 4, 0.5, 4.5));
	getStatistics().createHistogram(new TH1F("TOT_thr_1", "TOT on threshold 1 [ns]", 100, 20.0, 100.0));
	getStatistics().createHistogram(new TH1F("TOT_thr_2", "TOT on threshold 2 [ns]", 100, 20.0, 100.0));
	getStatistics().createHistogram(new TH1F("TOT_thr_3", "TOT on threshold 3 [ns]", 100, 20.0, 100.0));
	getStatistics().createHistogram(new TH1F("TOT_thr_4", "TOT on threshold 4 [ns]", 100, 20.0, 100.0));
}

//Module execution method
void Module2::exec(){

	//getting the data from event in propriate format
	if(auto timeWindow = dynamic_cast<const JPetTimeWindow*const>(getEvent())){

		map<int,vector<JPetSigCh>> sigChsPMMap;
		//map<int,vector<JPetRawSignal>> rawSignalsPMMap; 

		//map Signal Channels in this Time window according to PM they belong to
		const unsigned int nSigChs = timeWindow->getNumberOfSigCh();
		for (unsigned int i = 0; i < nSigChs; i++) {
			JPetSigCh sigCh = timeWindow->operator[](i);
			int pmt_id = sigCh.getPM().getID();
			auto search = sigChsPMMap.find(pmt_id);
			if(search == sigChsPMMap.end()){
				vector<JPetSigCh> tmp;
				tmp.push_back(sigCh);
				sigChsPMMap.insert(pair<int,vector<JPetSigCh>>(pmt_id,tmp));
			}else{
				search->second.push_back(sigCh);
			}
		}

		//reading Signal Channel Map and creating Raw Signals
		for(auto &sigChPair : sigChsPMMap){
			//int pmt_id = sigChPair.first;
			vector<JPetRawSignal> rawSignals = buildRawSignals(timeWindow->getIndex(), sigChPair.second);
			saveRawSignals(rawSignals);
			//rawSignalsPMMap.insert(pmt_id, rawSignals);
		}	
	}
}

//Module finish method
void Module2::terminate(){
	INFO("Module 2 ended.");
}

//sorting method
bool sortByTimeValue(JPetSigCh sig1, JPetSigCh sig2){
	return (sig1.getValue() < sig2.getValue());
}

//method creating Raw signals form vector of Signal Channels
vector<JPetRawSignal> Module2::buildRawSignals(Int_t timeWindowIndex, vector<JPetSigCh> sigChFromSamePM){
	
	vector<JPetSigCh> tmp;
	vector<vector<JPetSigCh>> thresholdSigCh;

	//initialisation with empty vectors
	for(int i=0;i<2*kNumOfThresholds;++i){
		thresholdSigCh.push_back(tmp);

	}

	//division into subvectors according to threshold number:
	//0-3 leading, 4-7 trailing
	for(JPetSigCh sigCh : sigChFromSamePM){
		if(sigCh.getType() == JPetSigCh::Leading){
			thresholdSigCh.at(sigCh.getThresholdNumber()-1).push_back(sigCh);
		}else if(sigCh.getType() == JPetSigCh::Trailing){
			thresholdSigCh.at(sigCh.getThresholdNumber()+kNumOfThresholds-1).push_back(sigCh);
		}
	}

	//probably not needed vector sorting according to Signal channel time values
	for(auto thrVec : thresholdSigCh){
		sort(thrVec.begin(), thrVec.end(), sortByTimeValue);
	}

	//constructing Raw Signals
	vector<JPetRawSignal> rawSigVec;
	while(thresholdSigCh.at(0).size()>0){
		
		JPetRawSignal *rawSig = new JPetRawSignal();
		rawSig->setTimeWindowIndex(timeWindowIndex);
		rawSig->setPM(thresholdSigCh.at(0).at(0).getPM());
		rawSig->setBarrelSlot(thresholdSigCh.at(0).at(0).getPM().getBarrelSlot());

		//first leading added by default
		rawSig->addPoint(thresholdSigCh.at(0).at(0));

		//looking for points from other thresholds that belong to the same leading edge
		//and search for equivalent trailing edge points

		//first thr trailing
		int closestTrailingSigCh0 = findTrailingSigCh(thresholdSigCh.at(0).at(0), thresholdSigCh.at(4));
		if(closestTrailingSigCh0 != -1) {
			double tot0 = thresholdSigCh.at(4).at(closestTrailingSigCh0).getValue() 
					- thresholdSigCh.at(0).at(0).getValue();
			getStatistics().getHisto1D("TOT_thr_1").Fill(tot0/1000.0);
			rawSig->addPoint(thresholdSigCh.at(4).at(closestTrailingSigCh0));
			thresholdSigCh.at(4).erase(thresholdSigCh.at(4).begin()+closestTrailingSigCh0);
		}

		//second thr leading and trailing
		int nextThrSigChIndex1 = findSigChOnNextThr(thresholdSigCh.at(0).at(0), thresholdSigCh.at(1));
		if(nextThrSigChIndex1 != -1) {

			int closestTrailingSigCh1 = findTrailingSigCh(thresholdSigCh.at(1).at(nextThrSigChIndex1), thresholdSigCh.at(5));
			if(closestTrailingSigCh1 != -1) {
				double tot1 = thresholdSigCh.at(5).at(closestTrailingSigCh1).getValue() 
						- thresholdSigCh.at(1).at(nextThrSigChIndex1).getValue();
				getStatistics().getHisto1D("TOT_thr_2").Fill(tot1/1000.0);
				rawSig->addPoint(thresholdSigCh.at(5).at(closestTrailingSigCh1));
				thresholdSigCh.at(5).erase(thresholdSigCh.at(5).begin()+closestTrailingSigCh1);
			}

			rawSig->addPoint(thresholdSigCh.at(1).at(nextThrSigChIndex1));
			thresholdSigCh.at(1).erase(thresholdSigCh.at(1).begin()+nextThrSigChIndex1);
		}

		//third thr leading and trailing
		int nextThrSigChIndex2 = findSigChOnNextThr(thresholdSigCh.at(0).at(0), thresholdSigCh.at(2));
		if(nextThrSigChIndex2 != -1) {

			int closestTrailingSigCh2 = findTrailingSigCh(thresholdSigCh.at(2).at(nextThrSigChIndex2), thresholdSigCh.at(6));
			if(closestTrailingSigCh2 != -1) {
				double tot2 = thresholdSigCh.at(6).at(closestTrailingSigCh2).getValue() 
						- thresholdSigCh.at(2).at(nextThrSigChIndex2).getValue();
				getStatistics().getHisto1D("TOT_thr_3").Fill(tot2/1000.0);
				rawSig->addPoint(thresholdSigCh.at(6).at(closestTrailingSigCh2));
				thresholdSigCh.at(6).erase(thresholdSigCh.at(6).begin()+closestTrailingSigCh2);
			}

			rawSig->addPoint(thresholdSigCh.at(2).at(nextThrSigChIndex2));
			thresholdSigCh.at(2).erase(thresholdSigCh.at(2).begin()+nextThrSigChIndex2);
		}

		//fourth thr leading and trailing
		int nextThrSigChIndex3 = findSigChOnNextThr(thresholdSigCh.at(0).at(0), thresholdSigCh.at(3));
		if(nextThrSigChIndex3 != -1) {

			int closestTrailingSigCh3 = findTrailingSigCh(thresholdSigCh.at(3).at(nextThrSigChIndex3), thresholdSigCh.at(7));
			if(closestTrailingSigCh3 != -1) {
				double tot3 = thresholdSigCh.at(7).at(closestTrailingSigCh3).getValue() 
						- thresholdSigCh.at(3).at(nextThrSigChIndex3).getValue();
				getStatistics().getHisto1D("TOT_thr_4").Fill(tot3/1000.0);
				rawSig->addPoint(thresholdSigCh.at(7).at(closestTrailingSigCh3));
				thresholdSigCh.at(7).erase(thresholdSigCh.at(7).begin()+closestTrailingSigCh3);
			}

			rawSig->addPoint(thresholdSigCh.at(3).at(nextThrSigChIndex3));
			thresholdSigCh.at(3).erase(thresholdSigCh.at(3).begin()+nextThrSigChIndex3);
		}

		//addingcreated Raw Signal to vector
		rawSigVec.push_back(*rawSig);
		thresholdSigCh.at(0).erase(thresholdSigCh.at(0).begin()+0);
	}

	//filling controll histograms
	getStatistics().getHisto1D("remainig_leading_sig_ch_per_thr").Fill(1,thresholdSigCh.at(0).size());
	getStatistics().getHisto1D("remainig_leading_sig_ch_per_thr").Fill(2,thresholdSigCh.at(1).size());
	getStatistics().getHisto1D("remainig_leading_sig_ch_per_thr").Fill(3,thresholdSigCh.at(2).size());
	getStatistics().getHisto1D("remainig_leading_sig_ch_per_thr").Fill(4,thresholdSigCh.at(3).size());
	getStatistics().getHisto1D("remainig_trailing_sig_ch_per_thr").Fill(1,thresholdSigCh.at(4).size());
	getStatistics().getHisto1D("remainig_trailing_sig_ch_per_thr").Fill(2,thresholdSigCh.at(5).size());
	getStatistics().getHisto1D("remainig_trailing_sig_ch_per_thr").Fill(3,thresholdSigCh.at(6).size());
	getStatistics().getHisto1D("remainig_trailing_sig_ch_per_thr").Fill(4,thresholdSigCh.at(7).size());
	
	//return argument
	return rawSigVec;
}

//method of finding Signal Channels that belong to the same leading edge 
//not more than some amount of ps away, defined in header file
int Module2::findSigChOnNextThr(JPetSigCh sigCh, vector<JPetSigCh> sigChVec){
	for(Int_t i = 0; i<sigChVec.size(); i++){
		if(fabs(sigCh.getValue()-sigChVec.at(i).getValue()) < SIGCH_EDGE_MAX_TIME){
			return i;
		}
	}
	return -1;
}

//method of finding trailing edge SigCh that suits certian leading edge SigCh
//not further away than amount in ps defined in header files
//if more than one trailing edge SigCh found, returning one with the smallest index
//that is equivalent of SigCh earliest in time
int Module2::findTrailingSigCh(JPetSigCh leadingSigCh, vector<JPetSigCh> trailingSigChVec){
	vector<int> trailingFoundIdices;
	for(Int_t i = 0; i<trailingSigChVec.size(); i++){
		if(fabs(leadingSigCh.getValue()-trailingSigChVec.at(i).getValue()) < SIGCH_LEAD_TRAIL_MAX_TIME)
			trailingFoundIdices.push_back(i);
	}
	if(trailingFoundIdices.size()==0) return -1;
	sort(trailingFoundIdices.begin(), trailingFoundIdices.end());
	return trailingFoundIdices.at(0);
}

//saving method
void Module2::saveRawSignals(vector<JPetRawSignal> sigChVec){
	assert(fWriter);
	for(JPetRawSignal sigCh : sigChVec){
		fWriter->write(sigCh);
	}
}

//other methods - TODO check if neccessary
void Module2::setWriter(JPetWriter* writer) {
	fWriter = writer;
}

void Module2::setParamManager(JPetParamManager* paramManager) {
	fParamManager = paramManager;
}

const JPetParamBank& Module2::getParamBank() const {
	return fParamManager->getParamBank();
}
