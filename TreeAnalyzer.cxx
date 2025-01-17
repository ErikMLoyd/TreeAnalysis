//////
// THIS IS A VERSION OF FlowAnalyzer.cxx THAT ANALYZES TREES RATHER THAN PICODSTS.
//
// This program reads trees made from picoDsts that have already passed 
// through event cuts. It performs an analysis of anisotropic
// flow coefficients. Many of the controls are, or should be, located
// within the config file that is supplied to the ConfigReader.
// The config files are used to maximize the modularity of the program
// and minimize hardcoding of settings/parameters. Event/track QA cuts, 
// bad runs, and centrality definitions should have been handled when 
// the trees were produced.
//
// Author: Cameron Racz
// Date: 2021
//////


//////
// Do not use vector::erase() to change any vectors. 
// This will invalidate any for() loops iterating over the vectors
// and make things much more complicated. For bad events after 
// creation of the "Event" vector, use the badEvent flag.
//////


// C++ headers
#include <iostream>
#include <vector>
#include <sys/resource.h>

// ROOT headers
#include "TROOT.h"
#include "TObject.h"
#include "TChain.h"
#include "TF1.h"
#include "TSystem.h"
#include "TKey.h"
#include "TStopwatch.h"

// EPD Util headers
#include "StRoot/StEpdUtil/StEpdGeom.h"

// Configuration file reader
#include "StRoot/ConfigReader/ConfigReader.h"

// My Util Header
#include "FlowUtils.h"


//=========================================================
//          SOME CONTROLS
//=========================================================
const Int_t CENT_BINS  = 16;             // Number of centrality bins to show (max 16)  LEAVE AT 16 FOR NOW, BEST FOR RESOLUTION STUFF
const Int_t FIRST_CENT = 16 - CENT_BINS;            // Starting point for centrality dependent plots

const Double_t D_M0_PI = 0.139571;   //Rest masses
const Double_t D_M0_KA = 0.493677;
const Double_t D_M0_PR = 0.938272;
const Double_t D_M0_DE = 1.875613;   // Deuteron
const Double_t D_M0_TR = 2.808921;   // Triton

Int_t RUN_ITERATION = 0;
// 0 = No correction info yet; save raw (Xn,Yn) distributions
// 1 = Correction file found, but only <Xn> and <Yn> for re-centering.
//     Also save <sin> <cos> at this step for shifting in the next step.
// 2 = Correction file found, and <sin> <cos> values found so that shifting can be performed.
//=========================================================
//          
//=========================================================

int main(int argc, char *argv[])
{
  TStopwatch* stopWatch = new TStopwatch();
  stopWatch->Start();

  std::cout << "Initializing..." << std::endl;

  TString inFile = argv[1];
  TString jobID  = argv[2];
  std::string configFileName = argv[3];
  TString correctionFileName = argv[4];
  TString resolutionFileName = argv[5];

  if (gSystem->AccessPathName(inFile)) { std::cout << "Error reading input file!" << std::endl; return 1;}

  //=========================================================
  //          Set up various files
  //=========================================================
  ConfigReader configs;
  configs.read(configFileName);
  if (configs.errorFound()) { std::cout << "There was an error reading the configurations! Aborting analysis!" << std::endl; return 1; }

  const Double_t ORDER_N = configs.order_n;   // Order of anisotropic flow (v_n)
  const Double_t ORDER_M = configs.order_m;   // Order of event plane angle (psi_m)
  const Double_t Y_MID   = configs.y_mid;     // Mid rapidity for the current energy
  TString ORDER_N_STR;
  TString ORDER_M_STR;
  ORDER_N_STR.Form("%.0f", ORDER_N);
  ORDER_M_STR.Form("%.0f", ORDER_M);
  const Double_t PSI_BOUNDS = TMath::Pi()/ORDER_M + 1;  // Boundaries for many histograms
  const Double_t Q_BOUNDS = 100;
  const Bool_t ODD_PLANE = ((int)ORDER_M % 2 == 1) ? true : false;

  //=== INITIALIZE TTREE
  Int_t N_track = 0;  // Max number of tracks in an event. Depends on energy and centrality definition!
  if      (configs.sqrt_s_NN == 3.0)  { N_track = 195;  }
  else if (configs.sqrt_s_NN == 7.2)  { N_track = 240;  }
  else if (configs.sqrt_s_NN == 19.6) { N_track = 2048; } // UPDATE THIS WHEN THE CENTRALITY IS OFFICIAL
  else
    {
      std::cout << "Unknown energy! N_track is not set!" << std::endl;
      return 1;
    }

  Int_t i_runID;
  Int_t i_eventID;
  Float_t f_bField;
  Float_t f_xvtx;
  Float_t f_yvtx;
  Float_t f_zvtx;
  UShort_t i_centrality;
  UShort_t i_trackNumber;
  Short_t PID[N_track];
  Short_t charge[N_track];
  Float_t Px[N_track];
  Float_t Py[N_track];
  Float_t Pz[N_track];
  UShort_t i_nEPDhits;
  Short_t EPDids[744];
  Float_t EPDnMip[744];
  Double_t tofBeta[N_track];
  Float_t dEdx[N_track];
  Int_t nHitsFit[N_track];
  Int_t nHitsPoss[N_track];


  TFile *inputFile = TFile::Open(inFile);
  if (!inputFile) { std::cout << "Input file could not be opened properly!" << std::endl; return 1; }

  TTree *tree = (TTree*)inputFile->Get("Autree");  
  tree->SetBranchAddress("runId", &i_runID);
  tree->SetBranchAddress("eventId",&i_eventID);
  tree->SetBranchAddress("bField",&f_bField);
  tree->SetBranchAddress("Vx",&f_xvtx);
  tree->SetBranchAddress("Vy",&f_yvtx);
  tree->SetBranchAddress("Vz",&f_zvtx);
  tree->SetBranchAddress("centrality",&i_centrality);
  tree->SetBranchAddress("tracknumber",&i_trackNumber);
  tree->SetBranchAddress("PID",&PID);
  tree->SetBranchAddress("Charge",&charge);
  tree->SetBranchAddress("Px",&Px);
  tree->SetBranchAddress("Py",&Py);
  tree->SetBranchAddress("Pz",&Pz);
  tree->SetBranchAddress("nEPDhits",&i_nEPDhits);
  tree->SetBranchAddress("EPDid",&EPDids);
  tree->SetBranchAddress("EPDnMip",&EPDnMip);
  tree->SetBranchAddress("tofBeta",&tofBeta);
  tree->SetBranchAddress("dEdx",&dEdx);
  tree->SetBranchAddress("nHitsFit",&nHitsFit);
  tree->SetBranchAddress("nHitsPoss",&nHitsPoss);

  // INPUT FILE FOR CORRECTION INFORMATION
  //TString correctionInputName = "correctionInfo_INPUT.root";
  TFile *correctionInputFile = TFile::Open(correctionFileName, "READ");
  if (!correctionInputFile)
    {
      RUN_ITERATION = 0;
      std::cout << "No correction file found." << std::endl
		<< "Re-centering and shifting will not be performed." << std::endl;
    }
  else
    {
      TKey *key;
      TIter next(correctionInputFile->GetListOfKeys());
      TProfile *profile;

      while( (key = (TKey*)next()) )
	{
	  TClass *cl = gROOT->GetClass(key->GetClassName());
	  
	  if (cl->InheritsFrom("TProfile"))
	    {
	      profile = (TProfile*)key->ReadObj();
	      if (profile->GetEntries() == 0)
		{
		  std::cout << "TProfiles are empty!" << std::endl
			    << "Re-centering will be performed and TProfiles will be filled." << std::endl;
		  RUN_ITERATION = 1;
		  break;
		}
	      else if (profile->GetEntries() != 0)
		{
		  std::cout << "Non-empty TProfiles found!" << std::endl
			    << "Re-centering and event plane shifting will be performed." << std::endl;
		  RUN_ITERATION = 2;
		  break;
		}
	    }
	}
    }


  // INPUT FILE FOR EVENT PLANE RESOLUTION INFORMATION
  Bool_t resolutionsFound = false;
  //TString resolutionInputName = "resolutionInfo_INPUT.root";
  TFile *resolutionInputFile;
  if (RUN_ITERATION == 2) 
    { 
      resolutionInputFile = TFile::Open(resolutionFileName, "READ"); 
      if (!resolutionInputFile) { std::cout << "No resolution file was found!" << std::endl; }
      else 
	{ 
	  resolutionsFound = true;
	  std::cout << "Resolution file found!" << std::endl; 
	}
    }

  // INPUT FILE FOR TPC EFFICIENCY CORRECTIONS
  TString tpcEfficiencyFileName = "results_eff_tpc.root";
  TFile *tpcEfficiencyFile;
  Bool_t efficienciesFound = false;
  TH2D *h2_ratio_pp;
  TH2D *h2_ratio_kp;
  TH2D *h2_ratio_km;
  TH2D *h2_ratio_pr;
  if (RUN_ITERATION == 2)
    {
      tpcEfficiencyFile = TFile::Open(tpcEfficiencyFileName, "READ");
      if (!tpcEfficiencyFile) { std::cout << "No TPC efficiency file was found! Efficiencies will default to 1!" << std::endl; }
      else 
	{ 
	  efficienciesFound = true;
	  std::cout << "TPC efficiency file was found!" << std::endl; 
	  h2_ratio_pp = (TH2D*)tpcEfficiencyFile->Get("h2_ratio_pp");
	  h2_ratio_kp = (TH2D*)tpcEfficiencyFile->Get("h2_ratio_kp");
	  h2_ratio_km = (TH2D*)tpcEfficiencyFile->Get("h2_ratio_km");
	  h2_ratio_pr = (TH2D*)tpcEfficiencyFile->Get("h2_ratio_pr");
	}
    }

  // OUTPUT FILE FOR CORRECTION INFORMATION
  TString correctionOutputName = "correctionInfo_OUTPUT_"+jobID+".root";
  TFile *correctionOutputFile;
  if (RUN_ITERATION == 0 || RUN_ITERATION == 1) { correctionOutputFile = new TFile(correctionOutputName, "RECREATE"); }

  // MAIN OUTPUT FILE
  TString outFile = jobID+".picoDst.result.root";
  TFile *outputFile = new TFile(outFile,"RECREATE");
  outputFile->cd();
  //=========================================================
  //          END file setup
  //=========================================================


  // HISTOGRAMS
  //TH1::SetDefaultSumw2(true);

  // temp variables when histogram bins/bounds depend on the energy
  int tempBins1 = 0;
  double tempLowBound1 = 0;
  double tempHighBound1 = 0;
  //int tempBins2 = 0;
  //double tempLowBound2 = 0;
  //double tempHighBound2 = 0;


  TH1D *h_PID = new TH1D("h_PID","Track IDs;ID;Tracks", 6, -1, 5);

  TH1D *h_eventCheck = (TH1D*)inputFile->Get("h_eventCheck");
  h_eventCheck->SetStats(0);

  TH1D *h_trackCheck = (TH1D*)inputFile->Get("h_trackCheck");
  h_trackCheck->SetStats(0);
  /*
  TH1D *h_eventCheck_EpdB = new TH1D("h_eventCheck_EpdB","EPD B Event Number;;Events", 2, 0, 2);
  //const char *eventSections_EpdB[2] = {"5 Hit Min", "9 Hit Min"};
  h_eventCheck_EpdB->SetStats(0);

  TH1D *h_simulationCheck = new TH1D ("h_simulationCheck", "N_{trk} with no TPC efficiency", 3, 0, 3);
  TH1D *h_simulationCheck_total = new TH1D ("h_simulationCheck_total", "Total N_{trk}", 3, 0, 3);
  */
  TH1D *h_nhits       = (TH1D*)inputFile->Get("h_nhits");
  TH1D *h_nhitsFit    = new TH1D("h_nhitsFit", "nHitsFit;Number of hits for fitting;Tracks", 50, 0, 50);
  TH1D *h_nhitsPoss   = new TH1D("h_nhitsPoss", "nHitsFit;Number of hits possible;Tracks", 50, 0, 50);
  TH1D *h_nhits_ratio = (TH1D*)inputFile->Get("h_nhits_ratio");
  TH1D *h_nhits_dEdx  = (TH1D*)inputFile->Get("h_nhits_dEdx");
  TH1D *h_DCA         = (TH1D*)inputFile->Get("h_DCA");

  TH1D *h_primTracks = new TH1D("h_primTracks","Raw Number of Primary Tracks;Tracks;Events", 200, 0, 200);

  TH1D *h_zvtx = (TH1D*)inputFile->Get("h_zvtx");

  TH1D *h_eta_s   = new TH1D("h_eta_s", "Particle #eta_{CM};#eta-#eta_{mid};Particles", 1200, -6, 6);
  TH1D *h_eta_TPC_s = new TH1D("h_eta_TPC_s", "TPC tracks' #eta_{CM};#eta-#eta_{mid};Particles", 500, -2.5, 2.5);

  TH1D *h_tileWeights = new TH1D("h_tileWeights", "EPD Tile Weights;Hits;nMIP Weights", 5, -1, 4);
  TH1D *h_centralities = (TH1D*)inputFile->Get("h_centralities");

  TH1D *h_trackmult = (TH1D*)inputFile->Get("h_trackmult");
  TH1D *h_refmult   = (TH1D*)inputFile->Get("h_refmult");
  TH1D *h_tofmult   = (TH1D*)inputFile->Get("h_tofmult");

  TH1D *h_pT  = (TH1D*)inputFile->Get("h_pT");
  TH1D *h_eta = (TH1D*)inputFile->Get("h_eta");
  TH1D *h_phi = (TH1D*)inputFile->Get("h_phi");
  
  //TH1D *h_tofBeta = new TH1D("h_tofBeta", "TOF #beta;#beta;Tracks", 150, 0, 1.5);
  //TH1D *h_m2 = new TH1D("h_m2", "m^{2};m^{2} (GeV^{2}/c^{4});Tracks", 1000, 0, 10);
  /*  
  TH1D *h_pp_dndm = new TH1D("h_pp_dndm", "#pi^{+} Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}", 60, 0, 3);
  TH1D *h_pm_dndm = new TH1D("h_pm_dndm", "#pi^{-} Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}", 60, 0, 3);
  TH1D *h_kp_dndm = new TH1D("h_kp_dndm", "K^{+} Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}",   60, 0, 3);
  TH1D *h_km_dndm = new TH1D("h_km_dndm", "K^{-} Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}",   60, 0, 3);
  TH1D *h_pr_dndm = new TH1D("h_pr_dndm", "Proton Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}",  60, 0, 3);
  TH1D *h_de_dndm = new TH1D("h_de_dndm", "Deuteron Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}",60, 0, 3);
  TH1D *h_tr_dndm = new TH1D("h_tr_dndm", "Triton Raw m_{T} Spectrum;m_{T}-m_{0} (GeV);dN/dm_{T}",  60, 0, 3);

  TH1D *h_pp_dndy = new TH1D("h_pp_dndy", "#pi^{+} Raw Rapidity Spectrum;y;dN/dy", 40, -2, 0);
  TH1D *h_pm_dndy = new TH1D("h_pm_dndy", "#pi^{-} Raw Rapidity Spectrum;y;dN/dy", 40, -2, 0);
  TH1D *h_kp_dndy = new TH1D("h_kp_dndy", "K^{+} Raw Rapidity Spectrum;y;dN/dy",   40, -2, 0);
  TH1D *h_km_dndy = new TH1D("h_km_dndy", "K^{-} Raw Rapidity Spectrum;y;dN/dy",   40, -2, 0);
  TH1D *h_pr_dndy = new TH1D("h_pr_dndy", "Proton Raw Rapidity Spectrum;y;dN/dy",  40, -2, 0);
  TH1D *h_de_dndy = new TH1D("h_de_dndy", "Deuteron Raw Rapidity Spectrum;y;dN/dy",40, -2, 0);
  TH1D *h_tr_dndy = new TH1D("h_tr_dndy", "Triton Raw Rapidity Spectrum;y;dN/dy",  40, -2, 0);

  TH1D *h_pp_pT = new TH1D("h_pp_pT", "#pi^{+} p_{T};p_{T} (GeV);", 100, 0, 5);
  TH1D *h_pm_pT = new TH1D("h_pm_pT", "#pi^{-} p_{T};p_{T} (GeV);", 100, 0, 5);
  TH1D *h_kp_pT = new TH1D("h_kp_pT", "K^{+} p_{T};p_{T} (GeV);",   100, 0, 5);
  TH1D *h_km_pT = new TH1D("h_km_pT", "K^{-} p_{T};p_{T} (GeV);",   100, 0, 5);
  TH1D *h_pr_pT = new TH1D("h_pr_pT", "Proton p_{T};p_{T} (GeV);",  100, 0, 5);
  TH1D *h_de_pT = new TH1D("h_de_pT", "Deuteron p_{T};p_{T} (GeV);",100, 0, 5);
  TH1D *h_tr_pT = new TH1D("h_tr_pT", "Triton p_{T};p_{T} (GeV);",  100, 0, 5);
  */

  TH1D *h_mult_pp = (TH1D*)inputFile->Get("h_mult_pp");
  TH1D *h_mult_pm = (TH1D*)inputFile->Get("h_mult_pm");
  TH1D *h_mult_kp = (TH1D*)inputFile->Get("h_mult_kp");
  TH1D *h_mult_km = (TH1D*)inputFile->Get("h_mult_km");
  TH1D *h_mult_pr = (TH1D*)inputFile->Get("h_mult_pr");
  TH1D *h_mult_de = (TH1D*)inputFile->Get("h_mult_de");
  TH1D *h_mult_tr = (TH1D*)inputFile->Get("h_mult_tr");

  TH1D *h_pT_pp = (TH1D*)inputFile->Get("h_pT_pp");
  TH1D *h_pT_pm = (TH1D*)inputFile->Get("h_pT_pm");
  TH1D *h_pT_kp = (TH1D*)inputFile->Get("h_pT_kp");
  TH1D *h_pT_km = (TH1D*)inputFile->Get("h_pT_km");
  TH1D *h_pT_pr = (TH1D*)inputFile->Get("h_pT_pr");
  TH1D *h_pT_de = (TH1D*)inputFile->Get("h_pT_de");
  TH1D *h_pT_tr = (TH1D*)inputFile->Get("h_pT_tr");

  TH1D *h_dndy_pp = (TH1D*)inputFile->Get("h_dndy_pp");
  TH1D *h_dndy_pm = (TH1D*)inputFile->Get("h_dndy_pm");
  TH1D *h_dndy_kp = (TH1D*)inputFile->Get("h_dndy_kp");
  TH1D *h_dndy_km = (TH1D*)inputFile->Get("h_dndy_km");
  TH1D *h_dndy_pr = (TH1D*)inputFile->Get("h_dndy_pr");
  TH1D *h_dndy_de = (TH1D*)inputFile->Get("h_dndy_de");
  TH1D *h_dndy_tr = (TH1D*)inputFile->Get("h_dndy_tr");

  TH1D *h_eta_pp = (TH1D*)inputFile->Get("h_eta_pp");
  TH1D *h_eta_pm = (TH1D*)inputFile->Get("h_eta_pm");
  TH1D *h_eta_kp = (TH1D*)inputFile->Get("h_eta_kp");
  TH1D *h_eta_km = (TH1D*)inputFile->Get("h_eta_km");
  TH1D *h_eta_pr = (TH1D*)inputFile->Get("h_eta_pr");
  TH1D *h_eta_de = (TH1D*)inputFile->Get("h_eta_de");
  TH1D *h_eta_tr = (TH1D*)inputFile->Get("h_eta_tr");

  TH1D *h_phi_pp = (TH1D*)inputFile->Get("h_phi_pp");
  TH1D *h_phi_pm = (TH1D*)inputFile->Get("h_phi_pm");
  TH1D *h_phi_kp = (TH1D*)inputFile->Get("h_phi_kp");
  TH1D *h_phi_km = (TH1D*)inputFile->Get("h_phi_km");
  TH1D *h_phi_pr = (TH1D*)inputFile->Get("h_phi_pr");
  TH1D *h_phi_de = (TH1D*)inputFile->Get("h_phi_de");
  TH1D *h_phi_tr = (TH1D*)inputFile->Get("h_phi_tr");

  TH1D *h_pp_mom = new TH1D("h_pp_mom", "#pi^{+} Total Momentum;|p| (GeV);", 100, 0, 5);
  TH1D *h_pm_mom = new TH1D("h_pm_mom", "#pi^{-} Total Momentum;|p| (GeV);", 100, 0, 5);
  TH1D *h_kp_mom = new TH1D("h_kp_mom", "K^{+} Total Momentum;|p| (GeV);",   100, 0, 5);
  TH1D *h_km_mom = new TH1D("h_km_mom", "K^{-} Total Momentum;|p| (GeV);",   100, 0, 5);
  TH1D *h_pr_mom = new TH1D("h_pr_mom", "Proton Total Momentum;|p| (GeV);",  100, 0, 5);
  TH1D *h_de_mom = new TH1D("h_de_mom", "Deuteron Total Momentum;|p| (GeV);",100, 0, 5);
  TH1D *h_tr_mom = new TH1D("h_tr_mom", "Triton Total Momentum;|p| (GeV);",  100, 0, 5);

  TH1D *h_psiTpc_RAW  = new TH1D("h_psiTpc_RAW", "Raw Event Plane Angles (m = "+ORDER_M_STR+", TPC);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiTpcA_RAW = new TH1D("h_psiTpcA_RAW", "Raw Event Plane Angles (m = "+ORDER_M_STR+", TPC A);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiTpcB_RAW = new TH1D("h_psiTpcB_RAW", "Raw Event Plane Angles (m = "+ORDER_M_STR+", TPC B);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpd_RAW  = new TH1D("h_psiEpd_RAW", "Raw Event Plane Angles (m = "+ORDER_M_STR+", EPD);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpdA_RAW = new TH1D("h_psiEpdA_RAW", "Raw Event Plane Angles (m = "+ORDER_M_STR+", EPD A);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpdB_RAW = new TH1D("h_psiEpdB_RAW", "Raw Event Plane Angles (m = "+ORDER_M_STR+", EPD B);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);

  TProfile *p_vn_EpdA = new TProfile("p_vn_EpdA", "v_{"+ORDER_N_STR+"} by Centrality (EPD A);Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_EpdB = new TProfile("p_vn_EpdB", "v_{"+ORDER_N_STR+"} by Centrality (EPD B);Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  //TProfile *p_vn_TpcA = new TProfile("p_vn_TpcA", "v_{"+ORDER_N_STR+"} by Centrality (TPC A);Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
  //				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_TpcB = new TProfile("p_vn_TpcB", "v_{"+ORDER_N_STR+"} by Centrality (TPC B);Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_Tpc_pT_0p2to2 = 
    new TProfile("p_vn_Tpc_pT_0p2to2", "v_{"+ORDER_N_STR+"} by Centrality (All TPC 0.2 < p_{T} < 2.0);Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
		 CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);

  TProfile *p_vn_pp = new TProfile("p_vn_pp", "#pi^{+} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_pm = new TProfile("p_vn_pm", "#pi^{-} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_kp = new TProfile("p_vn_kp", "K^{+} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_km = new TProfile("p_vn_km", "K^{-} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_pr = new TProfile("p_vn_pr", "Proton v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_de = new TProfile("p_vn_de", "Deuteron v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				   CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_tr = new TProfile("p_vn_tr", "Triton v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);

  // vn profiles at "extended" rapidity range 0.5 < y_CM < 1.0
  TProfile *p_vn_pp_ext = new TProfile("p_vn_pp_ext", "#pi^{+} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_pm_ext = new TProfile("p_vn_pm_ext", "#pi^{-} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_kp_ext = new TProfile("p_vn_kp_ext", "K^{+} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_km_ext = new TProfile("p_vn_km_ext", "K^{-} v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_pr_ext = new TProfile("p_vn_pr_ext", "Proton v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_de_ext = new TProfile("p_vn_de_ext", "Deuteron v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				       CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);
  TProfile *p_vn_tr_ext = new TProfile("p_vn_tr_ext", "Triton v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				       CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);

  // vn profiles at the "forward" raidity range y_CM < 0
  TProfile *p_vn_pr_for = new TProfile("p_vn_pr_for", "Proton v_{"+ORDER_N_STR+"} by Centrality;Centrality;<cos("+ORDER_N_STR+"(#phi - #psi_{"+ORDER_M_STR+"}))>", 
				    CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS);


  TH1D *h_phiRelative_pr = new TH1D("h_phiRelative_pr", "-0.1 < y_{CM}^{pr} < 0.1;#phi - #psi_{"+ORDER_M_STR+"};dN/d(#Delta#phi)", 100, -PSI_BOUNDS, PSI_BOUNDS);

  TH1D *h_psiEpdA_NoAuto = new TH1D("h_psiEpdA_NoAuto", "EP Angles, No Auto-Correlations (m = "+ORDER_M_STR+", EPD A);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);

  TH2D *h2_phiRelative_vs_yCM_midCent_pr 
    = new TH2D("h2_phiRelative_vs_yCM_midCent_pr", ";y-y_{mid};#phi-#psi_{"+ORDER_M_STR+"}", 20, -1, 1, 100, -TMath::Pi(), TMath::Pi());

  TH2D *h2_triCorr_vs_yCM_midCent_pr 
    = new TH2D("h2_triCorr_vs_yCM_midCent_pr", ";y-y_{mid};cos(3(#phi-#psi_{"+ORDER_M_STR+"})) / R_{3"+ORDER_M_STR+"}", 20, -1, 1, 100, -TMath::Pi(), TMath::Pi());

  // Differential Flow Profiles
  TProfile2D *p2_vn_yCM_cent_pp = new TProfile2D("p2_vn_yCM_cent_pp", "#pi^{+} v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_pm = new TProfile2D("p2_vn_yCM_cent_pm", "#pi^{-} v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_kp = new TProfile2D("p2_vn_yCM_cent_kp", "K^{+} v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_km = new TProfile2D("p2_vn_yCM_cent_km", "K^{-} v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_pr = new TProfile2D("p2_vn_yCM_cent_pr", "Proton v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_pr_symmetry = 
    new TProfile2D("p2_vn_yCM_cent_pr_symmetry", "Proton v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_de = new TProfile2D("p2_vn_yCM_cent_de", "Deuteron v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);
  TProfile2D *p2_vn_yCM_cent_tr = new TProfile2D("p2_vn_yCM_cent_tr", "Triton v_{"+ORDER_N_STR+"};Centrality;y-y_{mid}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 20, -1, 1);

  TProfile2D *p2_vn_pT_cent_pp = new TProfile2D("p2_vn_pT_cent_pp", "#pi^{+} v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);
  TProfile2D *p2_vn_pT_cent_pm = new TProfile2D("p2_vn_pT_cent_pm", "#pi^{-} v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);
  TProfile2D *p2_vn_pT_cent_kp = new TProfile2D("p2_vn_pT_cent_kp", "K^{+} v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);
  TProfile2D *p2_vn_pT_cent_km = new TProfile2D("p2_vn_pT_cent_km", "K^{-} v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);
  TProfile2D *p2_vn_pT_cent_pr = new TProfile2D("p2_vn_pT_cent_pr", "Proton v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);
  TProfile2D *p2_vn_pT_cent_de = new TProfile2D("p2_vn_pT_cent_de", "Deuteron v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);
  TProfile2D *p2_vn_pT_cent_tr = new TProfile2D("p2_vn_pT_cent_tr", "Triton v_{"+ORDER_N_STR+"};Centrality;p_{T}", CENT_BINS, FIRST_CENT, FIRST_CENT+CENT_BINS, 10, 0, 2);

  // Profiles for resolution terms
  TProfile *p_TpcAB = new TProfile("p_TpcAB","TPC A-B Correlations;Centrality;<cos("+ORDER_N_STR+"(#psi^{TPC,A}_{"+ORDER_M_STR+"}-#psi^{TPC,B}_{"+ORDER_M_STR+"}))>",
				   CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);

  TProfile *p_TpcAEpdA = new TProfile("p_TpcAEpdA","TPC A EPD A Correlations;Centrality;<cos("+ORDER_N_STR+"(#psi^{TPC,A}_{"+ORDER_M_STR+"}-#psi^{EPD,A}_{"+ORDER_M_STR+"}))>", 
				      CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile *p_TpcAEpdB = new TProfile("p_TpcAEpdB","TPC A EPD B Correlations;Centrality;<cos("+ORDER_N_STR+"(#psi^{TPC,A}_{"+ORDER_M_STR+"}-#psi^{EPD,B}_{"+ORDER_M_STR+"}))>",
				      CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);

  TProfile *p_TpcBEpdA = new TProfile("p_TpcBEpdA","TPC B EPD A Correlations;Centrality;<cos("+ORDER_N_STR+"(#psi^{TPC,B}_{"+ORDER_M_STR+"}-#psi^{EPD,A}_{"+ORDER_M_STR+"}))>",
				      CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile *p_TpcBEpdB = new TProfile("p_TpcBEpdB","TPC B EPD B Correlations;Centrality;<cos("+ORDER_N_STR+"(#psi^{TPC,B}_{"+ORDER_M_STR+"}-#psi^{EPD,B}_{"+ORDER_M_STR+"}))>",
				      CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);

  TProfile *p_EpdAEpdB = new TProfile("p_EpdAEpdB","EPD A EPD B Correlations;Centrality;<cos("+ORDER_N_STR+"(#psi^{EPD,A}_{"+ORDER_M_STR+"}-#psi^{EPD,B}_{"+ORDER_M_STR+"}))>", 
				      CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);

  tempBins1      = (configs.fixed_target) ? 400 : 800;
  tempLowBound1  = (configs.fixed_target) ? -6.0 : -6.0;
  tempHighBound1 = (configs.fixed_target) ? -2.0 : 6.0;
  TProfile2D *p2_pp_vs_eta = new TProfile2D("p2_pp_vs_eta","<TnMIP> for Supersectors vs #eta;#eta;Supersector", tempBins1, tempLowBound1, tempHighBound1, 12, 0.5, 12.5);
  TH2D *h2_trans_vtx     = (TH2D*)inputFile->Get("h2_trans_vtx");
  TH2D *h2_trans_vtx_cut = (TH2D*)inputFile->Get("h2_trans_vtx_cut");

  TH2D *h2_refmult_vs_trackmult = (TH2D*)inputFile->Get("h2_refmult_vs_trackmult");
  TH2D *h2_tofmult_vs_trackmult = (TH2D*)inputFile->Get("h2_tofmult_vs_trackmult");
  TH2D *h2_tofmult_vs_refmult   = (TH2D*)inputFile->Get("h2_tofmult_vs_refmult");

  TH2D *h2_hits_vs_cent_EpdA = new TH2D("h2_nHits_vs_cent_EpdA", "EPD A;Centrality;Hits", CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS, 50, 0, 50);
  TH2D *h2_hits_vs_cent_EpdB = new TH2D("h2_nHits_vs_cent_EpdB", "EPD B;Centrality;Hits", CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS, 50, 0, 50);
  TH2D *h2_hits_vs_cent_TpcB = new TH2D("h2_nHits_vs_cent_TpcB", "TPC B;Centrality;Hits", CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS, 50, 0, 50);

  TH2D *h2_dEdx_vs_qp = (TH2D*)inputFile->Get("h2_dEdx_vs_qp");
  TH2D *h2_dEdx_vs_qp_half = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_half");
  TH2D *h2_beta_vs_qp = (TH2D*)inputFile->Get("h2_beta_vs_qp");
  TH2D *h2_m2_vs_qp   = (TH2D*)inputFile->Get("h2_m2_vs_qp");

  TH2D *h2_dEdx_vs_qp_pp = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_pp");
  TH2D *h2_dEdx_vs_qp_pm = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_pm");
  TH2D *h2_dEdx_vs_qp_kp = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_kp");
  TH2D *h2_dEdx_vs_qp_km = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_km");
  TH2D *h2_dEdx_vs_qp_pr = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_pr");
  TH2D *h2_dEdx_vs_qp_de = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_de");
  TH2D *h2_dEdx_vs_qp_tr = (TH2D*)inputFile->Get("h2_dEdx_vs_qp_tr");

  TH2D *h2_beta_vs_qp_pp = (TH2D*)inputFile->Get("h2_beta_vs_qp_pp");
  TH2D *h2_beta_vs_qp_pm = (TH2D*)inputFile->Get("h2_beta_vs_qp_pm");
  TH2D *h2_beta_vs_qp_kp = (TH2D*)inputFile->Get("h2_beta_vs_qp_kp");
  TH2D *h2_beta_vs_qp_km = (TH2D*)inputFile->Get("h2_beta_vs_qp_km");
  TH2D *h2_beta_vs_qp_pr = (TH2D*)inputFile->Get("h2_beta_vs_qp_pr");
  TH2D *h2_beta_vs_qp_de = (TH2D*)inputFile->Get("h2_beta_vs_qp_de");
  TH2D *h2_beta_vs_qp_tr = (TH2D*)inputFile->Get("h2_beta_vs_qp_tr");

  TH2D *h2_m2_vs_qp_pp = (TH2D*)inputFile->Get("h2_m2_vs_qp_pp");
  TH2D *h2_m2_vs_qp_pm = (TH2D*)inputFile->Get("h2_m2_vs_qp_pm");
  TH2D *h2_m2_vs_qp_kp = (TH2D*)inputFile->Get("h2_m2_vs_qp_kp");
  TH2D *h2_m2_vs_qp_km = (TH2D*)inputFile->Get("h2_m2_vs_qp_km");
  TH2D *h2_m2_vs_qp_pr = (TH2D*)inputFile->Get("h2_m2_vs_qp_pr");
  TH2D *h2_m2_vs_qp_de = (TH2D*)inputFile->Get("h2_m2_vs_qp_de");
  TH2D *h2_m2_vs_qp_tr = (TH2D*)inputFile->Get("h2_m2_vs_qp_tr");
  
  tempBins1      = (configs.fixed_target) ?  300 :  600;
  tempLowBound1  = (configs.fixed_target) ? -2.2 : -2.5;
  tempHighBound1 = (configs.fixed_target) ?  0.2 :  2.5;
  TH2D *h2_phi_vs_eta_TPC = new TH2D("h2_phi_vs_eta_TPC", "TPC;#eta;#phi", tempBins1, tempLowBound1, tempHighBound1, 300, -4, 4);

  tempBins1      = (configs.fixed_target) ?  300 :  600;
  tempLowBound1  = (configs.fixed_target) ? -6.0 : -6.0;
  tempHighBound1 = (configs.fixed_target) ? -2.5 :  6.0;
  TH2D *h2_phi_vs_eta_EPD = new TH2D("h2_phi_vs_eta_EPD", "EPD;#eta;#phi", tempBins1, tempLowBound1, tempHighBound1, 300, -4, 4);

  TH2D *h2_pT_vs_yCM_pp = (TH2D*)inputFile->Get("h2_pT_vs_yCM_pp");
  TH2D *h2_pT_vs_yCM_pm = (TH2D*)inputFile->Get("h2_pT_vs_yCM_pm");
  TH2D *h2_pT_vs_yCM_kp = (TH2D*)inputFile->Get("h2_pT_vs_yCM_kp");
  TH2D *h2_pT_vs_yCM_km = (TH2D*)inputFile->Get("h2_pT_vs_yCM_km");
  TH2D *h2_pT_vs_yCM_pr = (TH2D*)inputFile->Get("h2_pT_vs_yCM_pr");
  TH2D *h2_pT_vs_yCM_de = (TH2D*)inputFile->Get("h2_pT_vs_yCM_de");
  TH2D *h2_pT_vs_yCM_tr = (TH2D*)inputFile->Get("h2_pT_vs_yCM_tr");


  // Here the name refers to the eta region that will be displayed/searched using the event plane angle from the opposite region
  tempBins1      = (configs.fixed_target) ?  50  :  100;
  tempLowBound1  = (configs.fixed_target) ? -2.0 : -2.3;
  tempHighBound1 = (configs.fixed_target) ?  0.0 :  2.3;
  TProfile2D *h2_v2ScanTpc = new TProfile2D("h2_v2ScanTpc", "<cos("+ORDER_N_STR+"(#phi^{TPC} - #psi^{EPD}_{"+ORDER_M_STR+"}))>;#eta;Centrality (%)", 
					    tempBins1, tempLowBound1, tempHighBound1, CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile2D *h2_v2ScanTpcEpdA = new TProfile2D("h2_v2ScanTpcEpdA", "<cos("+ORDER_N_STR+"(#phi^{TPC} - #psi^{EPD,A}_{"+ORDER_M_STR+"}))>;#eta;Centrality (%)", 
						tempBins1, tempLowBound1, tempHighBound1, CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile2D *h2_v2ScanTpcEpdB = new TProfile2D("h2_v2ScanTpcEpdB", "<cos("+ORDER_N_STR+"(#phi^{TPC} - #psi^{EPD,B}_{"+ORDER_M_STR+"}))>;#eta;Centrality (%)", 
						tempBins1, tempLowBound1, tempHighBound1, CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile2D *h2_v2ScanEpd = new TProfile2D("h2_v2ScanEpd", "<cos("+ORDER_N_STR+"(#phi^{EPD} - #psi^{TPC}_{"+ORDER_M_STR+"}))>;#eta;Centrality (%)", 
					    50, -5.2, -2.3, CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile2D *h2_v2ScanEpdTpcA = new TProfile2D("h2_v2ScanEpdTpcA", "<cos("+ORDER_N_STR+"(#phi^{EPD} - #psi^{TPC,A}_{"+ORDER_M_STR+"}))>;#eta;Centrality (%)", 
						50, -5.2, -2.3, CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  TProfile2D *h2_v2ScanEpdTpcB = new TProfile2D("h2_v2ScanEpdTpcB", "<cos("+ORDER_N_STR+"(#phi^{EPD} - #psi^{TPC,B}_{"+ORDER_M_STR+"}))>;#eta;Centrality (%)", 
						50, -5.2, -2.3, CENT_BINS, FIRST_CENT, FIRST_CENT + CENT_BINS);
  h2_v2ScanTpc->SetStats(0);
  h2_v2ScanTpcEpdA->SetStats(0);
  h2_v2ScanTpcEpdB->SetStats(0);
  h2_v2ScanEpd->SetStats(0);
  h2_v2ScanEpdTpcA->SetStats(0);
  h2_v2ScanEpdTpcB->SetStats(0);

  // The indices here are equivalent to the corresponding centrality ID
  const char *centralityBins[16] = {"75-80", "70-75", "65-70", "60-65", "55-60", "50-55", "45-50", "40-45", "35-40", "30-35", "25-30", "20-25", "15-20", "10-15", "5-10", "0-5"};

  // The indices here are opposite to the corresponding centrality ID (array is backward)
  //const char *centralityBins[16] = {"0-5", "5-10", "10-15" "15-20", "20-25", "25-30", "30-35", "35-40", "40-45", "45-50", "50-55", "55-60", "60-65", "65-70", "70-75", "75-80"};


  TH2D *h2_psiEpdATpcA = new TH2D("h2_psiEpdATpcA", "#psi^{EPD,A} vs #psi^{TPC,A} (Order "+ORDER_M_STR+");#psi^{TPC}_{A};#psi^{EPD}_{E}", 
				  200, -PSI_BOUNDS, PSI_BOUNDS, 200, -PSI_BOUNDS, PSI_BOUNDS);
  TH2D *h2_psiEpdBTpcA = new TH2D("h2_psiEpdBTpcA", "#psi^{EPD,B} vs #psi^{TPC,A} (Order "+ORDER_M_STR+");#psi^{TPC}_{A};#psi^{EPD}_{F}", 
				  200, -PSI_BOUNDS, PSI_BOUNDS, 200, -PSI_BOUNDS, PSI_BOUNDS);

  TH2D *h2_psiEpdATpcB = new TH2D("h2_psiEpdATpcB", "#psi^{EPD,A} vs #psi^{TPC,B} (Order "+ORDER_M_STR+");#psi^{TPC}_{B};#psi^{EPD}_{E}", 
				  200, -PSI_BOUNDS, PSI_BOUNDS, 200, -PSI_BOUNDS, PSI_BOUNDS);
  TH2D *h2_psiEpdBTpcB = new TH2D("h2_psiEpdBTpcB", "#psi^{EPD,B} vs #psi^{TPC,B} (Order "+ORDER_M_STR+");#psi^{TPC}_{B};#psi^{EPD}_{F}", 
				  200, -PSI_BOUNDS, PSI_BOUNDS, 200, -PSI_BOUNDS, PSI_BOUNDS);

  TH2D *h2_psiTpcATpcB = new TH2D("h2_psiTpcATpcB", "#psi^{TPC,A} vs #psi^{TPC,B} (Order "+ORDER_M_STR+");#psi^{TPC}_{B};#psi^{TPC}_{A}", 
				  200, -PSI_BOUNDS, PSI_BOUNDS, 200, -PSI_BOUNDS, PSI_BOUNDS);

  TH2D *h2_psiEpdAEpdB = new TH2D("h2_psiEpdAEpdB", "#psi^{EPD,A} vs #psi^{EPD,B} (Order "+ORDER_M_STR+");#psi^{EPD}_{F};#psi^{EPD}_{E}", 
				  200, -PSI_BOUNDS, PSI_BOUNDS, 200, -PSI_BOUNDS, PSI_BOUNDS);



  TH1D *h_XnTpc  = new TH1D("h_XnTpc", "X_n Distribution (TPC);X_n;Events",    250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnTpc  = new TH1D("h_YnTpc", "Y_n Distribution (TPC);Y_n;Events",    250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnTpcA = new TH1D("h_XnTpcA", "X_n Distribution (TPC A);X_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnTpcA = new TH1D("h_YnTpcA", "Y_n Distribution (TPC A);Y_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnTpcB = new TH1D("h_XnTpcB", "X_n Distribution (TPC B);X_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnTpcB = new TH1D("h_YnTpcB", "Y_n Distribution (TPC B);Y_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnEpd  = new TH1D("h_XnEpd", "X_n Distribution (EPD);X_n;Events",    250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnEpd  = new TH1D("h_YnEpd", "Y_n Distribution (EPD);Y_n;Events",    250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnEpdA = new TH1D("h_XnEpdA", "X_n Distribution (EPD A);X_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnEpdA = new TH1D("h_YnEpdA", "Y_n Distribution (EPD A);Y_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnEpdB = new TH1D("h_XnEpdB", "X_n Distribution (EPD B);X_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnEpdB = new TH1D("h_YnEpdB", "Y_n Distribution (EPD B);Y_n;Events", 250, -Q_BOUNDS, Q_BOUNDS);

  // CORRECTION HISTOGRAMS
  TProfile *p_sinAvgsTpc  = new TProfile("p_sinAvgsTpc", "Sin Averages (TPC);j (Correction term);<sin(jn#psi^{TPC}_{n})>",      configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_cosAvgsTpc  = new TProfile("p_cosAvgsTpc", "Cos Averages (TPC);j (Correction term);<sin(jn#psi^{TPC}_{n})>",      configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_sinAvgsTpcA = new TProfile("p_sinAvgsTpcA", "Sin Averages (TPC A);j (Correction term);<sin(jn#psi^{TPC,A}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_cosAvgsTpcA = new TProfile("p_cosAvgsTpcA", "Cos Averages (TPC A);j (Correction term);<sin(jn#psi^{TPC,A}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_sinAvgsTpcB = new TProfile("p_sinAvgsTpcB", "Sin Averages (TPC B);j (Correction term);<sin(jn#psi^{TPC,B}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_cosAvgsTpcB = new TProfile("p_cosAvgsTpcB", "Cos Averages (TPC B);j (Correction term);<sin(jn#psi^{TPC,B}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_sinAvgsEpd  = new TProfile("p_sinAvgsEpd", "Sin Averages (EPD);j (Correction term);<sin(jn#psi^{EPD}_{n})>",      configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_cosAvgsEpd  = new TProfile("p_cosAvgsEpd", "Cos Averages (EPD);j (Correction term);<sin(jn#psi^{EPD}_{n})>",      configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_sinAvgsEpdA = new TProfile("p_sinAvgsEpdA", "Sin Averages (EPD A);j (Correction term);<sin(jn#psi^{EPD,A}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_cosAvgsEpdA = new TProfile("p_cosAvgsEpdA", "Cos Averages (EPD A);j (Correction term);<sin(jn#psi^{EPD,A}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_sinAvgsEpdB = new TProfile("p_sinAvgsEpdB", "Sin Averages (EPD B);j (Correction term);<sin(jn#psi^{EPD,B}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);
  TProfile *p_cosAvgsEpdB = new TProfile("p_cosAvgsEpdB", "Cos Averages (EPD B);j (Correction term);<sin(jn#psi^{EPD,B}_{n})>", configs.shift_terms, 1, configs.shift_terms + 1);

  // RECENTERED (RC) HISTOGRAMS
  TH1D *h_XnTpc_RC  = new TH1D("h_XnTpc_RC", "Re-centered X_n Distribution (TPC);X_n;Events",    200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnTpc_RC  = new TH1D("h_YnTpc_RC", "Re-centered Y_n Distribution (TPC);Y_n;Events",    200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnTpcA_RC = new TH1D("h_XnTpcA_RC", "Re-centered X_n Distribution (TPC A);X_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnTpcA_RC = new TH1D("h_YnTpcA_RC", "Re-centered Y_n Distribution (TPC A);Y_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnTpcB_RC = new TH1D("h_XnTpcB_RC", "Re-centered X_n Distribution (TPC B);X_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnTpcB_RC = new TH1D("h_YnTpcB_RC", "Re-centered Y_n Distribution (TPC B);Y_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnEpd_RC  = new TH1D("h_XnEpd_RC", "Re-centered X_n Distribution (EPD);X_n;Events",    200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnEpd_RC  = new TH1D("h_YnEpd_RC", "Re-centered Y_n Distribution (EPD);Y_n;Events",    200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnEpdA_RC = new TH1D("h_XnEpdA_RC", "Re-centered X_n Distribution (EPD A);X_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnEpdA_RC = new TH1D("h_YnEpdA_RC", "Re-centered Y_n Distribution (EPD A);Y_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_XnEpdB_RC = new TH1D("h_XnEpdB_RC", "Re-centered X_n Distribution (EPD B);X_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);
  TH1D *h_YnEpdB_RC = new TH1D("h_YnEpdB_RC", "Re-centered Y_n Distribution (EPD B);Y_n;Events", 200, -Q_BOUNDS, Q_BOUNDS);

  TH1D *h_psiTpc_RC  = new TH1D("h_psiTpc_RC", "Re-centered Event Plane Angles (m = "+ORDER_M_STR+", TPC);#psi_{"+ORDER_M_STR+"};Events",    400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiTpcA_RC = new TH1D("h_psiTpcA_RC", "Re-centered Event Plane Angles (m = "+ORDER_M_STR+", TPC A);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiTpcB_RC = new TH1D("h_psiTpcB_RC", "Re-centered Event Plane Angles (m = "+ORDER_M_STR+", TPC B);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpd_RC  = new TH1D("h_psiEpd_RC", "Re-centered Event Plane Angles (m = "+ORDER_M_STR+", EPD);#psi_{"+ORDER_M_STR+"};Events",    400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpdA_RC = new TH1D("h_psiEpdA_RC", "Re-centered Event Plane Angles (m = "+ORDER_M_STR+", EPD A);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpdB_RC = new TH1D("h_psiEpdB_RC", "Re-centered Event Plane Angles (m = "+ORDER_M_STR+", EPD B);#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);

  // RECENTERED AND SHIFTED HISTOGRAMS
  TH1D *h_psiTpc_FLAT  = new TH1D("h_psiTpc_FLAT", "Flattened Event Plane Angle (TPC, order "+ORDER_M_STR+");#psi_{"+ORDER_M_STR+"};Events",    400, -PSI_BOUNDS, PSI_BOUNDS);      
  TH1D *h_psiTpcA_FLAT = new TH1D("h_psiTpcA_FLAT", "Flattened Event Plane Angle (TPC A, order "+ORDER_M_STR+");#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiTpcB_FLAT = new TH1D("h_psiTpcB_FLAT", "Flattened Event Plane Angle (TPC B, order "+ORDER_M_STR+");#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpd_FLAT  = new TH1D("h_psiEpd_FLAT", "Flattened Event Plane Angle (EPD, order "+ORDER_M_STR+");#psi_{"+ORDER_M_STR+"};Events",    400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpdA_FLAT = new TH1D("h_psiEpdA_FLAT", "Flattened Event Plane Angle (EPD A, order "+ORDER_M_STR+");#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);
  TH1D *h_psiEpdB_FLAT = new TH1D("h_psiEpdB_FLAT", "Flattened Event Plane Angle (EPD B, order "+ORDER_M_STR+");#psi_{"+ORDER_M_STR+"};Events", 400, -PSI_BOUNDS, PSI_BOUNDS);


  FlowUtils::Event eventInfo;
  FlowUtils::Particle particleInfo;
  StEpdGeom *epdGeom = new StEpdGeom();

  Int_t events2read = tree->GetEntries();

  std::cout << "Setup complete, beginning analysis on " << events2read << " events..." << std::endl;

  // EVENT LOOP
  for (Long64_t ievent = 0; ievent < events2read; ievent++)
    {
      eventInfo.reset();

      tree->GetEntry(ievent);

      Int_t nTracks = (Int_t)i_trackNumber;
      if (nTracks < 5) continue;                // Preliminary cut to hopefully speed things up a bit. This cut repeated below also.

      eventInfo.centID = i_centrality;

      TVector3 pVtx(f_xvtx, f_yvtx, f_zvtx);
      Double_t d_px;
      Double_t d_py;
      Double_t d_pz;
      Double_t d_pT;
      Double_t d_mom;
      Double_t d_eta;
      Double_t d_phi;
      //Double_t d_tofBeta;
      //Double_t d_dEdx;
      Int_t i_nHitsFit;
      Int_t i_nHitsPoss;
      Short_t s_PID;
      Short_t s_charge;

      // TRACK LOOP OVER PRIMARY TRACKS
      for(Int_t iTrk = 0; iTrk < nTracks; iTrk++)
	{
	  particleInfo.reset();

	  eventInfo.primTracks++;

	  d_px  = Px[iTrk];
	  d_py  = Py[iTrk];
	  d_pz  = Pz[iTrk];
	  s_PID = PID[iTrk];
	  s_charge = charge[iTrk];
	  d_phi = FlowUtils::phi(Px[iTrk], Py[iTrk]);
	  d_eta = FlowUtils::pseudorapidity(Px[iTrk], Py[iTrk], Pz[iTrk]);
	  d_pT  = FlowUtils::transMomentum(Px[iTrk], Py[iTrk]);
	  d_mom = FlowUtils::totalMomentum(Px[iTrk], Py[iTrk], Pz[iTrk]);
	  //d_tofBeta = tofBeta[iTrk];
	  //d_dEdx = dEdx[iTrk];
	  i_nHitsFit = nHitsFit[iTrk];
	  i_nHitsPoss = nHitsPoss[iTrk];


	  h_PID->Fill((Double_t)s_PID);
	  h_nhitsFit->Fill(i_nHitsFit);
	  h_nhitsPoss->Fill(i_nHitsPoss);

	  // Get event planes from the TPC here before the TOF cut
	  if (s_charge != 0)
	    {
	      eventInfo.nTracksTpc++;

	      particleInfo.phi = d_phi;
	      particleInfo.eta = d_eta;
	      particleInfo.pT  = d_pT;

	      h2_phi_vs_eta_TPC->Fill(d_eta, d_phi);
	      
	      if (ODD_PLANE)
		{
		  if (d_eta > Y_MID)        // Account for Q vector sign change past mid-rapidity.
		    {
		      eventInfo.XnTpc += d_pT * TMath::Cos(ORDER_M * d_phi);
		      eventInfo.YnTpc += d_pT * TMath::Sin(ORDER_M * d_phi);
		    }
		  else if (d_eta < Y_MID)
		    {
		      eventInfo.XnTpc -= d_pT * TMath::Cos(ORDER_M * d_phi);
		      eventInfo.YnTpc -= d_pT * TMath::Sin(ORDER_M * d_phi);
		    }
		}
	      else
		{
		  eventInfo.XnTpc += d_pT * TMath::Cos(ORDER_M * d_phi);
		  eventInfo.YnTpc += d_pT * TMath::Sin(ORDER_M * d_phi);
		}


	      if (d_eta > configs.tpc_A_low_eta && d_eta < configs.tpc_A_high_eta)          // TPC A
		{
		  eventInfo.nTracksTpcA++;
		  particleInfo.isInTpcA = true;	  
		  particleInfo.weight = d_pT;

		  if (ODD_PLANE)
		    {
		      if (d_eta > Y_MID)        // Account for Q vector sign change past mid-rapidity.
			{
			  eventInfo.XnTpcA += d_pT * TMath::Cos(ORDER_M * d_phi);
			  eventInfo.YnTpcA += d_pT * TMath::Sin(ORDER_M * d_phi);
			}
		      else if (d_eta < Y_MID)
			{
			  eventInfo.XnTpcA -= d_pT * TMath::Cos(ORDER_M * d_phi);
			  eventInfo.YnTpcA -= d_pT * TMath::Sin(ORDER_M * d_phi);
			}
		    }
		  else
		    {
		      eventInfo.XnTpcA += d_pT * TMath::Cos(ORDER_M * d_phi);
		      eventInfo.YnTpcA += d_pT * TMath::Sin(ORDER_M * d_phi);
		    }
		} // End TPC A
	      if (d_eta > configs.tpc_B_low_eta && d_eta < configs.tpc_B_high_eta)     // TPC B
		{
		  eventInfo.nTracksTpcB++;
		  particleInfo.isInTpcB = true;
		  particleInfo.weight = d_pT;

		  if (ODD_PLANE)
		    {
		      if (d_eta > Y_MID)        // Account for Q vector sign change past mid-rapidity.
			{
			  eventInfo.XnTpcB += d_pT * TMath::Cos(ORDER_M * d_phi);
			  eventInfo.YnTpcB += d_pT * TMath::Sin(ORDER_M * d_phi);
			}
		      else if (d_eta < Y_MID)
			{
			  eventInfo.XnTpcB -= d_pT * TMath::Cos(ORDER_M * d_phi);
			  eventInfo.YnTpcB -= d_pT * TMath::Sin(ORDER_M * d_phi);
			}
		    }
		  else
		    {
		      eventInfo.XnTpcB += d_pT * TMath::Cos(ORDER_M * d_phi);
		      eventInfo.YnTpcB += d_pT * TMath::Sin(ORDER_M * d_phi);
		    }
		} // End TPC B


	      Bool_t pion     = (s_PID == 0);
	      Bool_t kaon     = (s_PID == 1);
	      Bool_t proton   = (s_PID == 2);
	      Bool_t deuteron = (s_PID == 3);
	      Bool_t triton   = (s_PID == 4);
	      Double_t d_rapidity;

	      if (pion)
		{
		  if (s_charge > 0) 
		    {
		      d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_PI);

		      h2_pT_vs_yCM_pp->Fill(d_rapidity - Y_MID, d_pT);
			  
		      if (d_rapidity - Y_MID > configs.yCM_pid_pi_low && d_rapidity - Y_MID < configs.yCM_pid_pi_high && 
			  d_pT >= configs.pt_pid_pi_low && d_pT <= configs.pt_pid_pi_high)
			{
			  particleInfo.ppTag = true;
			  particleInfo.rapidity = d_rapidity;
			  
			  //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_PI, h_pp_dndy, h_pp_dndm, h2_pp_MvsY);
			  //h2_y_vs_eta->Fill(d_eta, d_rapidity);
			  //h2_y_vs_eta_pp->Fill(d_eta, d_rapidity);
			  //h_pp_pT->Fill(d_pT);
			  h_pp_mom->Fill(d_mom);
			}
		    }
		  else if (s_charge < 0) 
		    {
		      d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_PI);

		      h2_pT_vs_yCM_pm->Fill(d_rapidity - Y_MID, d_pT);

		      if (d_rapidity - Y_MID > configs.yCM_pid_pi_low && d_rapidity - Y_MID < configs.yCM_pid_pi_high && 
			  d_pT >= configs.pt_pid_pi_low && d_pT <= configs.pt_pid_pi_high)
			{
			  particleInfo.pmTag = true;
			  particleInfo.rapidity = d_rapidity;

			  //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_PI, h_pm_dndy, h_pm_dndm, h2_pm_MvsY);
			  //h2_y_vs_eta->Fill(d_eta, d_rapidity);
			  //h2_y_vs_eta_pm->Fill(d_eta, d_rapidity);
			  //h_pm_pT->Fill(d_pT);
			  h_pm_mom->Fill(d_mom);
			}
		    }
		}
	      else if (kaon)
		{
		  if (s_charge > 0) 
		    {
		      d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_KA);

		      h2_pT_vs_yCM_kp->Fill(d_rapidity - Y_MID, d_pT);

		      if (d_rapidity - Y_MID > configs.yCM_pid_ka_low && d_rapidity - Y_MID < configs.yCM_pid_ka_high && 
			  d_pT >= configs.pt_pid_ka_low && d_pT <= configs.pt_pid_ka_high)
			{
			  particleInfo.kpTag = true;
			  particleInfo.rapidity = d_rapidity;

			  //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_KA, h_kp_dndy, h_kp_dndm, h2_kp_MvsY);
			  //h2_y_vs_eta->Fill(d_eta, d_rapidity);
			  //h2_y_vs_eta_kp->Fill(d_eta, d_rapidity);
			  //h_kp_pT->Fill(d_pT);
			  h_kp_mom->Fill(d_mom);
			}
		    }
		  else if (s_charge < 0)		 
		    {
		      d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_KA);

		      h2_pT_vs_yCM_km->Fill(d_rapidity - Y_MID, d_pT);

		      if (d_rapidity - Y_MID > configs.yCM_pid_ka_low && d_rapidity - Y_MID < configs.yCM_pid_ka_high && 
			  d_pT >= configs.pt_pid_ka_low && d_pT <= configs.pt_pid_ka_high)
			{
			  particleInfo.kmTag = true;
			  particleInfo.rapidity = d_rapidity;

			  //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_KA, h_km_dndy, h_km_dndm, h2_km_MvsY);
			  //h2_y_vs_eta->Fill(d_eta, d_rapidity);
			  //h2_y_vs_eta_km->Fill(d_eta, d_rapidity);
			  //h_km_pT->Fill(d_pT);
			  h_km_mom->Fill(d_mom);
			}
		    }
		}
	      else if (proton)
		{
		  d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_PR);

		  h2_pT_vs_yCM_pr->Fill(d_rapidity - Y_MID, d_pT);

		  if (d_rapidity - Y_MID > configs.yCM_pid_pr_low && d_rapidity - Y_MID < configs.yCM_pid_pr_high && 
		      d_pT >= configs.pt_pid_pr_low && d_pT <= configs.pt_pid_pr_high)  // Wide acceptance, trim during fills
		    {
		      particleInfo.prTag = true;
		      particleInfo.rapidity = d_rapidity;

		      // y cuts mixed here, systematics won't be right for these plots but it probably won't matter.
		      if (d_rapidity - Y_MID > configs.yCM_flow_pr_low && d_rapidity - Y_MID < configs.yCM_pid_pr_high && 
			  d_pT >= configs.pt_flow_pr_low && d_pT <= configs.pt_flow_pr_high)
			{
			  //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_PR, h_pr_dndy, h_pr_dndm, h2_pr_MvsY);
			  //h2_y_vs_eta->Fill(d_eta, d_rapidity);
			  //h2_y_vs_eta_pr->Fill(d_eta, d_rapidity);
			  //h_pr_pT->Fill(d_pT);
			  h_pr_mom->Fill(d_mom);
			}
		    }
		}
	      else if (deuteron)
		{
		  d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_DE);
		  
		  h2_pT_vs_yCM_de->Fill(d_rapidity - Y_MID, d_pT);
		  //h2_dEdx_vs_qp_half_postZdCut->Fill(s_charge * d_mom, d_dEdx);

		  if (d_rapidity - Y_MID > configs.yCM_pid_de_low && d_rapidity - Y_MID < configs.yCM_pid_de_high && 
		      d_pT >= configs.pt_pid_de_low && d_pT <= configs.pt_pid_de_high)
		    {
		      particleInfo.deTag = true;
		      particleInfo.rapidity = d_rapidity;

		      //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_DE, h_de_dndy, h_de_dndm, h2_de_MvsY);
		      //h2_y_vs_eta->Fill(d_eta, d_rapidity);
		      //h2_y_vs_eta_de->Fill(d_eta, d_rapidity);
		      //h_de_pT->Fill(d_pT);
		      h_de_mom->Fill(d_mom);
		    }
		}
	      else if (triton)
		{
		  d_rapidity = FlowUtils::rapidity(d_px, d_py, d_pz, D_M0_TR);

		  h2_pT_vs_yCM_tr->Fill(d_rapidity - Y_MID, d_pT);
		  //h2_dEdx_vs_qp_half_postZtCut->Fill(s_charge * d_mom, d_dEdx);

		  if (d_rapidity - Y_MID > configs.yCM_pid_tr_low && d_rapidity - Y_MID < configs.yCM_pid_tr_high && 
		      d_pT >= configs.pt_pid_tr_low && d_pT <= configs.pt_pid_tr_high)
		    {
		      particleInfo.trTag = true;
		      particleInfo.rapidity = d_rapidity;

		      //FlowUtils::fillRawSpect(d_px, d_py, d_pz, D_M0_TR, h_tr_dndy, h_tr_dndm, h2_tr_MvsY);
		      //h2_y_vs_eta->Fill(d_eta, d_rapidity);
		      //h2_y_vs_eta_tr->Fill(d_eta, d_rapidity);
		      //h_tr_pT->Fill(d_pT);
		      h_tr_mom->Fill(d_mom);
		    }
		}
	      
	      eventInfo.tpcParticles.push_back(particleInfo);
	    }// End if(s_charge != 0)
	}//End TPC track loop
      particleInfo.reset();


      //=========================================================
      //                EPD STUFF
      //=========================================================
      Short_t tileID;
      TVector3 tileVector;     // Vector from vertex to center of tile that was hit
      Int_t tileSector;
      Int_t tileRow;
      Double_t tileWeight;
      Double_t tileEta;
      Double_t tilePhi;
      Double_t tilenMip;

      FlowUtils::Particle epdParticleInfo;
      for (UShort_t iEpdHit = 0; iEpdHit < i_nEPDhits; iEpdHit++)
	{
	  epdParticleInfo.reset();

	  tileID = EPDids[iEpdHit];
	  //if (tileID > 0) continue;      // Exclude the West side
	  Bool_t epdAside = (tileID < 0);
	  Bool_t epdBside = (configs.fixed_target) ? (tileID < 0) : (tileID > 0); // EPD B is on the same side as A in FXT
	  
	  tileVector = epdGeom->TileCenter(tileID) - pVtx;
	  tileSector = FlowUtils::epdSector(tileID);
	  tileRow = FlowUtils::epdRow(tileID);
	  tileEta = tileVector.Eta();
	  tilePhi = tileVector.Phi();
	  tilenMip = EPDnMip[iEpdHit];
	  tileWeight = (tilenMip > configs.epd_threshold) ? ( (tilenMip > configs.epd_max_weight)?configs.epd_max_weight:tilenMip ) : 0;

	  if (epdAside)
	    {
	      eventInfo.nHitsEpd++;
	      if (ODD_PLANE)
		{
		  if (tileEta > Y_MID)        // Account for Q vector sign change past mid-rapidity.
		    {
		      eventInfo.XnEpd += tileWeight * TMath::Cos(ORDER_M * tilePhi);
		      eventInfo.YnEpd += tileWeight * TMath::Sin(ORDER_M * tilePhi);
		    }
		  else if (tileEta < Y_MID)
		    {
		      eventInfo.XnEpd -= tileWeight * TMath::Cos(ORDER_M * tilePhi);
		      eventInfo.YnEpd -= tileWeight * TMath::Sin(ORDER_M * tilePhi);
		    }
		}
	      else
		{
		  eventInfo.XnEpd += tileWeight * TMath::Cos(ORDER_M * tilePhi);
		  eventInfo.YnEpd += tileWeight * TMath::Sin(ORDER_M * tilePhi);
		}
	    }

	  if (epdAside && tileRow >= configs.epdA_inner_row && tileRow <= configs.epdA_outer_row)
	    {
	      eventInfo.nHitsEpdA++;
	      epdParticleInfo.isInEpdA = true;
	      epdParticleInfo.phi    = tilePhi;
	      epdParticleInfo.eta    = tileEta;
	      epdParticleInfo.weight = tileWeight;

	      h_tileWeights->Fill(tileWeight);
	      h2_phi_vs_eta_EPD->Fill(tileEta, tilePhi);
	      p2_pp_vs_eta->Fill(tileEta, tileSector, tileWeight);

	      if (ODD_PLANE)
		{
		  if (tileEta > Y_MID)        // Account for Q vector sign change past mid-rapidity.
		    {
		      eventInfo.XnEpdA += tileWeight * TMath::Cos(ORDER_M * tilePhi);
		      eventInfo.YnEpdA += tileWeight * TMath::Sin(ORDER_M * tilePhi);
		    }
		  else if (tileEta < Y_MID)
		    {
		      eventInfo.XnEpdA -= tileWeight * TMath::Cos(ORDER_M * tilePhi);
		      eventInfo.YnEpdA -= tileWeight * TMath::Sin(ORDER_M * tilePhi);
		    }
		}
	      else
		{
		  eventInfo.XnEpdA += tileWeight * TMath::Cos(ORDER_M * tilePhi);
		  eventInfo.YnEpdA += tileWeight * TMath::Sin(ORDER_M * tilePhi);
		}


	      eventInfo.epdParticles.push_back(epdParticleInfo);
	    }
	  else if (epdBside && tileRow >= configs.epdB_inner_row && tileRow <= configs.epdB_outer_row)
	    {
	      eventInfo.nHitsEpdB++;
	      epdParticleInfo.isInEpdB = true;
	      epdParticleInfo.phi    = tilePhi;
	      epdParticleInfo.eta    = tileEta;
	      epdParticleInfo.weight = tileWeight;

	      h_tileWeights->Fill(tileWeight);
	      h2_phi_vs_eta_EPD->Fill(tileEta, tilePhi);
	      p2_pp_vs_eta->Fill(tileEta, tileSector, tileWeight);

	      if (ODD_PLANE)
		{
		  if (tileEta > Y_MID)        // Account for Q vector sign change past mid-rapidity.
		    {
		      eventInfo.XnEpdB += tileWeight * TMath::Cos(ORDER_M * tilePhi);
		      eventInfo.YnEpdB += tileWeight * TMath::Sin(ORDER_M * tilePhi);
		    }
		  else if (tileEta < Y_MID)
		    {
		      eventInfo.XnEpdB -= tileWeight * TMath::Cos(ORDER_M * tilePhi);
		      eventInfo.YnEpdB -= tileWeight * TMath::Sin(ORDER_M * tilePhi);
		    }
		}
	      else
		{
		  eventInfo.XnEpdB += tileWeight * TMath::Cos(ORDER_M * tilePhi);
		  eventInfo.YnEpdB += tileWeight * TMath::Sin(ORDER_M * tilePhi);
		}

	      eventInfo.epdParticles.push_back(epdParticleInfo);
	    }
	}// End EPD hit loop
      epdParticleInfo.reset();
      //=========================================================
      //            END EPD STUFF
      //=========================================================


      if (eventInfo.nTracksTpcA < configs.min_tracks) continue;
      if (eventInfo.nTracksTpcB < configs.min_tracks) continue;
      if (eventInfo.nHitsEpd    < configs.min_tracks) continue;
      if (eventInfo.nHitsEpdA   < configs.min_tracks) continue;
      //if (eventInfo.nHitsEpdB   >= configs.min_tracks) h_eventCheck_EpdB->Fill(0);//h_eventCheck_EpdB->Fill(eventSections_EpdB[0], 1);
      //if (eventInfo.nHitsEpdB   >= configs.min_tracks+4) h_eventCheck_EpdB->Fill(1);//h_eventCheck_EpdB->Fill(eventSections_EpdB[1], 1);
      if (configs.fixed_target && eventInfo.nHitsEpdB < configs.min_tracks+4) continue;
      else if (!configs.fixed_target && eventInfo.nHitsEpdB < configs.min_tracks) continue;
      
      FlowUtils::checkZeroQ(eventInfo);
      if (eventInfo.badEvent) continue;


      FlowUtils::getAllPsi(eventInfo, ORDER_M);


      // Fill eta/phi distributions here since this is past all possible cuts.
      for (unsigned int i = 0; i < eventInfo.tpcParticles.size(); i++)
	{
	  h_eta_s->Fill(eventInfo.tpcParticles.at(i).eta - Y_MID);
	  h_eta_TPC_s->Fill(eventInfo.tpcParticles.at(i).eta - Y_MID);
	}
      for (unsigned int i = 0; i < eventInfo.epdParticles.size(); i++)
	{ h_eta_s->Fill(eventInfo.epdParticles.at(i).eta - Y_MID); }

      h2_hits_vs_cent_EpdA->Fill(eventInfo.centID, eventInfo.nHitsEpdA);
      h2_hits_vs_cent_EpdB->Fill(eventInfo.centID, eventInfo.nHitsEpdB);
      h2_hits_vs_cent_TpcB->Fill(eventInfo.centID, eventInfo.nTracksTpcB);

      h_primTracks->Fill(eventInfo.primTracks);
      h_centralities->Fill(eventInfo.centID);

      h_XnTpc->Fill(eventInfo.XnTpc);
      h_YnTpc->Fill(eventInfo.YnTpc);
      h_XnTpcA->Fill(eventInfo.XnTpcA);
      h_YnTpcA->Fill(eventInfo.YnTpcA);
      h_XnTpcB->Fill(eventInfo.XnTpcB);
      h_YnTpcB->Fill(eventInfo.YnTpcB);
      h_XnEpd->Fill(eventInfo.XnEpd);
      h_YnEpd->Fill(eventInfo.YnEpd);
      h_XnEpdA->Fill(eventInfo.XnEpdA);
      h_YnEpdA->Fill(eventInfo.YnEpdA);
      h_XnEpdB->Fill(eventInfo.XnEpdB);
      h_YnEpdB->Fill(eventInfo.YnEpdB);

      h_psiTpc_RAW->Fill(eventInfo.psiTpc);
      h_psiTpcA_RAW->Fill(eventInfo.psiTpcA);
      h_psiTpcB_RAW->Fill(eventInfo.psiTpcB);
      h_psiEpd_RAW->Fill(eventInfo.psiEpd);
      h_psiEpdA_RAW->Fill(eventInfo.psiEpdA);
      h_psiEpdB_RAW->Fill(eventInfo.psiEpdB);



      //=========================================================
      //          Re-centering (Xn, Yn) Distributions
      //=========================================================

      if (RUN_ITERATION == 1 || RUN_ITERATION == 2)
	{
	  FlowUtils::recenterQ(eventInfo, correctionInputFile, ORDER_M);

	  if (eventInfo.badEvent) continue;

	  h_XnTpc_RC->Fill(eventInfo.XnTpc);
	  h_XnTpcA_RC->Fill(eventInfo.XnTpcA);
	  h_XnTpcB_RC->Fill(eventInfo.XnTpcB);
	  h_XnEpd_RC->Fill(eventInfo.XnEpd);
	  h_XnEpdA_RC->Fill(eventInfo.XnEpdA);
	  h_XnEpdB_RC->Fill(eventInfo.XnEpdB);

	  h_YnTpc_RC->Fill(eventInfo.YnTpc);
	  h_YnTpcA_RC->Fill(eventInfo.YnTpcA);
	  h_YnTpcB_RC->Fill(eventInfo.YnTpcB);
	  h_YnEpd_RC->Fill(eventInfo.YnEpd);
	  h_YnEpdA_RC->Fill(eventInfo.YnEpdA);
	  h_YnEpdB_RC->Fill(eventInfo.YnEpdB);

	  h_psiTpc_RC->Fill(eventInfo.psiTpc);
	  h_psiTpcA_RC->Fill(eventInfo.psiTpcA);
	  h_psiTpcB_RC->Fill(eventInfo.psiTpcB);
	  h_psiEpd_RC->Fill(eventInfo.psiEpd);
	  h_psiEpdA_RC->Fill(eventInfo.psiEpdA);
	  h_psiEpdB_RC->Fill(eventInfo.psiEpdB);

	  // Accumulate terms for averages over the re-centered angles for event plane angle shifting
	  for (int j = 1; j <= configs.shift_terms; j++)
	    {
	      p_sinAvgsTpc->Fill(j,  TMath::Sin((Double_t)j * ORDER_M * eventInfo.psiTpc));
	      p_cosAvgsTpc->Fill(j,  TMath::Cos((Double_t)j * ORDER_M * eventInfo.psiTpc));
	      p_sinAvgsTpcA->Fill(j, TMath::Sin((Double_t)j * ORDER_M * eventInfo.psiTpcA));
	      p_cosAvgsTpcA->Fill(j, TMath::Cos((Double_t)j * ORDER_M * eventInfo.psiTpcA));
	      p_sinAvgsTpcB->Fill(j, TMath::Sin((Double_t)j * ORDER_M * eventInfo.psiTpcB));
	      p_cosAvgsTpcB->Fill(j, TMath::Cos((Double_t)j * ORDER_M * eventInfo.psiTpcB));
	      p_sinAvgsEpd->Fill(j,  TMath::Sin((Double_t)j * ORDER_M * eventInfo.psiEpd));
	      p_cosAvgsEpd->Fill(j,  TMath::Cos((Double_t)j * ORDER_M * eventInfo.psiEpd));
	      p_sinAvgsEpdA->Fill(j, TMath::Sin((Double_t)j * ORDER_M * eventInfo.psiEpdA));
	      p_cosAvgsEpdA->Fill(j, TMath::Cos((Double_t)j * ORDER_M * eventInfo.psiEpdA));
	      p_sinAvgsEpdB->Fill(j, TMath::Sin((Double_t)j * ORDER_M * eventInfo.psiEpdB));
	      p_cosAvgsEpdB->Fill(j, TMath::Cos((Double_t)j * ORDER_M * eventInfo.psiEpdB));
	    }
	}
      //=========================================================
      //          End Re-centering
      //=========================================================



      //=========================================================
      //          Event Plane Angle Shifting and Flow
      //=========================================================

      if (RUN_ITERATION == 2)
	{
	  FlowUtils::shiftPsi(eventInfo, correctionInputFile, ORDER_M, configs.shift_terms);

	  h_psiTpc_FLAT->Fill(eventInfo.psiTpc);
	  h_psiTpcA_FLAT->Fill(eventInfo.psiTpcA);
	  h_psiTpcB_FLAT->Fill(eventInfo.psiTpcB);
	  h_psiEpd_FLAT->Fill(eventInfo.psiEpd);
	  h_psiEpdA_FLAT->Fill(eventInfo.psiEpdA);
	  h_psiEpdB_FLAT->Fill(eventInfo.psiEpdB);
	  //=========================================================
	  //          End Event Plane Angle Shifting
	  //=========================================================


	  // 2D Correlations between event planes
	  h2_psiEpdATpcA->Fill(eventInfo.psiTpcA,eventInfo.psiEpdA);
	  h2_psiEpdBTpcA->Fill(eventInfo.psiTpcA,eventInfo.psiEpdB);

	  h2_psiEpdATpcB->Fill(eventInfo.psiTpcB,eventInfo.psiEpdA);
	  h2_psiEpdBTpcB->Fill(eventInfo.psiTpcB,eventInfo.psiEpdB);

	  h2_psiTpcATpcB->Fill(eventInfo.psiTpcB,eventInfo.psiTpcA);

	  h2_psiEpdAEpdB->Fill(eventInfo.psiEpdB,eventInfo.psiEpdA);
	  //


	  // 1D correlation averages used in calculating resolution using the 3 sub-event method
	  p_TpcAB->Fill(eventInfo.centID,    TMath::Cos(ORDER_N * (eventInfo.psiTpcA - eventInfo.psiTpcB)));

	  p_TpcAEpdA->Fill(eventInfo.centID, TMath::Cos(ORDER_N * (eventInfo.psiTpcA - eventInfo.psiEpdA)));
	  p_TpcAEpdB->Fill(eventInfo.centID, TMath::Cos(ORDER_N * (eventInfo.psiTpcA - eventInfo.psiEpdB)));
	  p_TpcBEpdA->Fill(eventInfo.centID, TMath::Cos(ORDER_N * (eventInfo.psiTpcB - eventInfo.psiEpdA)));
	  p_TpcBEpdB->Fill(eventInfo.centID, TMath::Cos(ORDER_N * (eventInfo.psiTpcB - eventInfo.psiEpdB)));

	  p_EpdAEpdB->Fill(eventInfo.centID, TMath::Cos(ORDER_N * (eventInfo.psiEpdA - eventInfo.psiEpdB)));
	  //


	  //=========================================================
	  //          v_n Scan Plots
	  //=========================================================
	  // 2D searches through eta and centrality for correlations between detectors
	  // SHOULD PROBABLY ONLY USE THIS SECTION IF THE EPD REGIONS COVER THE WHOLE EPD!! Otherwise there might be gaps or undercounting in some bins.
	  Int_t tpcHits = eventInfo.tpcParticles.size();
	  Int_t epdHits = eventInfo.epdParticles.size();
	  Double_t phiTpc;
	  Double_t etaTpc;
	  Double_t phiEpd;
	  Double_t etaEpd;
	  Double_t psiTpc  = eventInfo.psiTpc;
	  Double_t psiEpd  = eventInfo.psiEpd;
	  Double_t psiEpdA = eventInfo.psiEpdA;
	  Double_t psiEpdB = eventInfo.psiEpdB;
	  Double_t psiTpcA = eventInfo.psiTpcA;
	  Double_t psiTpcB = eventInfo.psiTpcB;
	  Int_t centralityID = eventInfo.centID;

	  for (int j = 0; j < epdHits; j++)
	    {
	      phiEpd = eventInfo.epdParticles.at(j).phi;
	      etaEpd = eventInfo.epdParticles.at(j).eta;

	      h2_v2ScanEpd->Fill(etaEpd, centralityID, TMath::Cos(ORDER_M * (phiEpd - psiTpc)));
	      h2_v2ScanEpdTpcA->Fill(etaEpd, centralityID, TMath::Cos(ORDER_M * (phiEpd - psiTpcA)));
	      h2_v2ScanEpdTpcB->Fill(etaEpd, centralityID, TMath::Cos(ORDER_M * (phiEpd - psiTpcB)));
	      //h2_phiSearchEpd->Fill(phiEpd, centralityID);
	    }
	  for (int j = 0; j < tpcHits; j++)
	    {
	      phiTpc = eventInfo.tpcParticles.at(j).phi;
	      etaTpc = eventInfo.tpcParticles.at(j).eta;

	      h2_v2ScanTpc->Fill(etaTpc, centralityID, TMath::Cos(ORDER_M * (phiTpc - psiEpd)));
	      h2_v2ScanTpcEpdA->Fill(etaTpc, centralityID, TMath::Cos(ORDER_M * (phiTpc - psiEpdA)));
	      h2_v2ScanTpcEpdB->Fill(etaTpc, centralityID, TMath::Cos(ORDER_M * (phiTpc - psiEpdB)));
	      //h2_phiSearchTpc->Fill(phiTpc, centralityID);
	    }
	  //=========================================================
	  //          End v_n Scan Plots
	  //=========================================================


	  //=========================================================
	  //        Flow Calculations
	  //=========================================================
	  if (resolutionsFound)
	    {
	      Double_t jthWeight;
	      Double_t jthPhi;
	      Double_t jthpT;
	      Double_t jthRapidity;
	      Double_t psi = eventInfo.psiEpdA;
	      Int_t centID = eventInfo.centID;

	      if (centID < 4) continue;  // ONLY LOOKING AT CENTRALITY 60% AND LOWER

	      TH1D *resolutionHistogram = (TH1D*)resolutionInputFile->Get("h_resolutions");
	      Double_t resolution = resolutionHistogram->GetBinContent(centID+1);

	      // v2 from EPD A
	      for (UInt_t j = 0; j < eventInfo.epdParticles.size(); j++)  // Loop through the j number of EPD A hits
		{
		  if (eventInfo.epdParticles.at(j).isInEpdA)
		    {
		      jthWeight = eventInfo.epdParticles.at(j).weight;
		      jthPhi    = eventInfo.epdParticles.at(j).phi;

		      Double_t newXn = eventInfo.XnEpdA - jthWeight * TMath::Cos(ORDER_M * jthPhi);   // For event i, remove the jth particle from event plane
		      Double_t newYn = eventInfo.YnEpdA - jthWeight * TMath::Sin(ORDER_M * jthPhi);
		      Double_t newPsi = TMath::ATan2(newYn, newXn) / ORDER_M;
		      //eventInfo.eventPlanesEpdA.push_back(newPsi);
		      h_psiEpdA_NoAuto->Fill(newPsi);

		      // Add contribution to v_n from the jth particle using the event plane that omits the jth particle:
		      p_vn_EpdA->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - newPsi)) / resolution);
		    }
		  else if (eventInfo.epdParticles.at(j).isInEpdB)
		    {
		      jthPhi = eventInfo.epdParticles.at(j).phi;

		      p_vn_EpdB->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / resolution);
		    }
		}


	      for (UInt_t j = 0; j < eventInfo.tpcParticles.size(); j++)
		{
		  jthPhi = eventInfo.tpcParticles.at(j).phi;
		  jthpT  = eventInfo.tpcParticles.at(j).pT;
		  jthRapidity = eventInfo.tpcParticles.at(j).rapidity;

		  //h_simulationCheck_total->Fill(1);
		  
		  Double_t tpcEfficiency = 1;  // Default
		  if (efficienciesFound)
		    {
		      if (eventInfo.tpcParticles.at(j).ppTag)      tpcEfficiency = FlowUtils::getTpcEff(jthRapidity - Y_MID, jthpT, h2_ratio_pp);
		      else if (eventInfo.tpcParticles.at(j).pmTag) tpcEfficiency = FlowUtils::getTpcEff(jthRapidity - Y_MID, jthpT, h2_ratio_pp);
		      else if (eventInfo.tpcParticles.at(j).kpTag) tpcEfficiency = FlowUtils::getTpcEff(jthRapidity - Y_MID, jthpT, h2_ratio_kp);
		      else if (eventInfo.tpcParticles.at(j).kmTag) tpcEfficiency = FlowUtils::getTpcEff(jthRapidity - Y_MID, jthpT, h2_ratio_km);
		      else if (eventInfo.tpcParticles.at(j).prTag) tpcEfficiency = FlowUtils::getTpcEff(jthRapidity - Y_MID, jthpT, h2_ratio_pr);
		    }

		  //if (tpcEfficiency == -1) { h_simulationCheck->Fill(1); continue; }

		  // ALL CHARGED TRACKS
		  if (jthpT > 0.2 && jthpT < 2.0)
		    { p_vn_Tpc_pT_0p2to2->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }

		  // v2 from TPC B and relative jthPhi angles for dN/dphi fitting
		  if (eventInfo.tpcParticles.at(j).isInTpcB)
		    { p_vn_TpcB->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }

		  // PI+
		  if (eventInfo.tpcParticles.at(j).ppTag)
		    {
		      p2_vn_yCM_cent_pp->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

		      if (jthRapidity - Y_MID > configs.yCM_flow_pi_low && jthRapidity - Y_MID < configs.yCM_flow_pi_high)  // only 0 < y_cm < 0.5
			{ 
			  p_vn_pp->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_pp->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_pi_low && jthRapidity - Y_MID < configs.yCM_ext_flow_pi_high)  // only 0.5 <= y_cm < 1.0
			{ p_vn_pp_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		  // PI-
		  else if (eventInfo.tpcParticles.at(j).pmTag)
		    {
		      p2_vn_yCM_cent_pm->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

		      if (jthRapidity - Y_MID > configs.yCM_flow_pi_low && jthRapidity - Y_MID < configs.yCM_flow_pi_high)  // only 0 < y_cm < 0.5
			{ 
			  p_vn_pm->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_pm->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_pi_low && jthRapidity - Y_MID < configs.yCM_ext_flow_pi_high)  // only 0.5 <= y_cm < 1.0
			{ p_vn_pm_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		  // K+
		  else if (eventInfo.tpcParticles.at(j).kpTag)
		    {
		      p2_vn_yCM_cent_kp->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

		      if (jthRapidity - Y_MID > configs.yCM_flow_ka_low && jthRapidity - Y_MID < configs.yCM_flow_ka_high)  // only 0 < y_cm < 0.5
			{ 
			  p_vn_kp->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_kp->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_ka_low && jthRapidity - Y_MID < configs.yCM_ext_flow_ka_high)  // only 0.5 <= y_cm < 1.0
			{ p_vn_kp_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		  // K-
		  else if (eventInfo.tpcParticles.at(j).kmTag)
		    {
		      p2_vn_yCM_cent_km->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));		      

		      if (jthRapidity - Y_MID > configs.yCM_flow_ka_low && jthRapidity - Y_MID < configs.yCM_flow_ka_high)  // only 0 < y_cm < 0.5
			{ 
			  p_vn_km->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_km->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_ka_low && jthRapidity - Y_MID < configs.yCM_ext_flow_ka_high)  // only 0.5 <= y_cm < 1.0
			{ p_vn_km_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		  // PROTON
		  else if (eventInfo.tpcParticles.at(j).prTag)
		    {
		      // RAPIDITY DEPENDENT STUFF
		      if (jthRapidity - Y_MID > configs.yCM_dep_flow_pr_low && jthRapidity - Y_MID < configs.yCM_dep_flow_pr_high && 
			  jthpT > configs.pt_ydep_flow_pr_low && jthpT < configs.pt_ydep_flow_pr_high)
			{ p2_vn_yCM_cent_pr->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }

		      // NORMAL ACCEPTANCE 0 < y_cm < 0.5
		      if (jthRapidity - Y_MID > configs.yCM_flow_pr_low && jthRapidity - Y_MID < configs.yCM_flow_pr_high &&
			  jthpT > configs.pt_flow_pr_low && jthpT < configs.pt_flow_pr_high)
			{ 
			  p_vn_pr->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_pr->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

			  //p3_vn_pT_yCM_cent_pr->Fill(centID, jthRapidity-Y_MID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      // EXTENDED RAPIDITY 0.5 <= y_cm < 1.0
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_pr_low && jthRapidity - Y_MID < configs.yCM_ext_flow_pr_high &&
			       jthpT > configs.pt_ext_flow_pr_low && jthpT < configs.pt_ext_flow_pr_high)
			{ p_vn_pr_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }

		      // RAPIDITY SYMMETRIC ACCEPTANCE REGION
		      if (jthRapidity - Y_MID > configs.yCM_sym_flow_pr_low && jthRapidity - Y_MID < configs.yCM_sym_flow_pr_high && 
			  jthpT > configs.pt_sym_flow_pr_low && jthpT < configs.pt_sym_flow_pr_high)
			{
			  p2_vn_yCM_cent_pr_symmetry->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));


			  if (jthRapidity - Y_MID > -0.1 && jthRapidity - Y_MID < 0.1)
			    { h_phiRelative_pr->Fill(jthPhi - psi); }

			  //p3_vn_pT_yCM_cent_pr_symm->Fill(centID, jthRapidity-Y_MID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

			  if (centID >= 8 && centID <= 13)
			    {
			      h2_phiRelative_vs_yCM_midCent_pr->Fill(jthRapidity - Y_MID, jthPhi - psi);
			      h2_triCorr_vs_yCM_midCent_pr->Fill(jthRapidity - Y_MID, TMath::Cos(3.0 * (jthPhi - psi)) / (resolution * tpcEfficiency));
			    }
			}// End rapidity symmetric region

		      // ONLY FORWARD ACCEPTANCE REGION
		      if (jthRapidity - Y_MID > configs.yCM_for_flow_pr_low && jthRapidity - Y_MID < configs.yCM_for_flow_pr_high && 
			  jthpT > configs.pt_for_flow_pr_low && jthpT < configs.pt_for_flow_pr_high)
			{ p_vn_pr_for->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		  // DEUTERON
		  if (eventInfo.tpcParticles.at(j).deTag)
		    {
		      p2_vn_yCM_cent_de->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

		      if (jthRapidity - Y_MID > configs.yCM_flow_de_low && jthRapidity - Y_MID < configs.yCM_flow_de_high)  // only 0 < y_cm < 0.5
			{ 
			  p_vn_de->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_de->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_de_low && jthRapidity - Y_MID < configs.yCM_ext_flow_de_high)  // only 0.5 <= y_cm < 1.0
			{ p_vn_de_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		  // TRITON
		  if (eventInfo.tpcParticles.at(j).trTag)
		    {
		      p2_vn_yCM_cent_tr->Fill(centID, jthRapidity - Y_MID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));

		      if (jthRapidity - Y_MID > configs.yCM_flow_tr_low && jthRapidity - Y_MID < configs.yCM_flow_tr_high)  // only 0 < y_cm < 0.5
			{ 
			  p_vn_tr->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); 
			  p2_vn_pT_cent_tr->Fill(centID, jthpT, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency));
			}
		      else if (jthRapidity - Y_MID >= configs.yCM_ext_flow_tr_low && jthRapidity - Y_MID < configs.yCM_ext_flow_tr_high)  // only 0.5 <= y_cm < 1.0
			{ p_vn_tr_ext->Fill(centID, TMath::Cos(ORDER_N * (jthPhi - psi)) / (resolution * tpcEfficiency)); }
		    }
		}// End tpc particles loop
	    }// End if(resolutionsFound)
	  //=========================================================
	  //            End Flow Calculations
	  //=========================================================
	}// End if(RUN_ITERATION == 2)
    }//END EVENT LOOP
  eventInfo.reset();

  // Switch axis labels on some plots
  // Put in centrality percentages
  Int_t labelIndex;
  for (int i = 1; i <= CENT_BINS; i++) 
    {
      labelIndex = FIRST_CENT + i - 1;
      h2_v2ScanTpc->GetYaxis()->SetBinLabel(i, centralityBins[labelIndex]); 
      h2_v2ScanTpcEpdA->GetYaxis()->SetBinLabel(i, centralityBins[labelIndex]); 
      h2_v2ScanTpcEpdB->GetYaxis()->SetBinLabel(i, centralityBins[labelIndex]); 
      h2_v2ScanEpd->GetYaxis()->SetBinLabel(i, centralityBins[labelIndex]);
      h2_v2ScanEpdTpcA->GetYaxis()->SetBinLabel(i, centralityBins[labelIndex]);
      h2_v2ScanEpdTpcB->GetYaxis()->SetBinLabel(i, centralityBins[labelIndex]);
    }

  h_eventCheck->Write();
  h_trackCheck->Write();
  h_nhits->Write();
  h_nhits_ratio->Write();
  h_nhits_dEdx->Write();
  h_DCA->Write();
  h_zvtx->Write();
  h_centralities->Write();
  h_trackmult->Write();
  h_refmult->Write();
  h_tofmult->Write();
  h_pT->Write();
  h_eta->Write();
  h_phi->Write();
  
  h_mult_pp->Write();
  h_mult_pm->Write();
  h_mult_kp->Write();
  h_mult_km->Write();
  h_mult_pr->Write();
  h_mult_de->Write();
  h_mult_tr->Write();

  h_pT_pp->Write();
  h_pT_pm->Write();
  h_pT_kp->Write();
  h_pT_km->Write();
  h_pT_pr->Write();
  h_pT_de->Write();
  h_pT_tr->Write();

  h_dndy_pp->Write();
  h_dndy_pm->Write();
  h_dndy_kp->Write();
  h_dndy_km->Write();
  h_dndy_pr->Write();
  h_dndy_de->Write();
  h_dndy_tr->Write();

  h_eta_pp->Write();
  h_eta_pm->Write();
  h_eta_kp->Write();
  h_eta_km->Write();
  h_eta_pr->Write();
  h_eta_de->Write();
  h_eta_tr->Write();

  h_phi_pp->Write();
  h_phi_pm->Write();
  h_phi_kp->Write();
  h_phi_km->Write();
  h_phi_pr->Write();
  h_phi_de->Write();
  h_phi_tr->Write();

  h2_trans_vtx->Write();
  h2_trans_vtx_cut->Write();
  h2_refmult_vs_trackmult->Write();
  h2_tofmult_vs_trackmult->Write();
  h2_tofmult_vs_refmult->Write();

  h2_dEdx_vs_qp->Write();
  h2_dEdx_vs_qp_half->Write();
  h2_beta_vs_qp->Write();
  h2_m2_vs_qp->Write();

  h2_dEdx_vs_qp_pp->Write();
  h2_dEdx_vs_qp_pm->Write();
  h2_dEdx_vs_qp_kp->Write();
  h2_dEdx_vs_qp_km->Write();
  h2_dEdx_vs_qp_pr->Write();
  h2_dEdx_vs_qp_de->Write();
  h2_dEdx_vs_qp_tr->Write();

  h2_beta_vs_qp_pp->Write();
  h2_beta_vs_qp_pm->Write();
  h2_beta_vs_qp_kp->Write();
  h2_beta_vs_qp_km->Write();
  h2_beta_vs_qp_pr->Write();
  h2_beta_vs_qp_de->Write();
  h2_beta_vs_qp_tr->Write();

  h2_m2_vs_qp_pp->Write();
  h2_m2_vs_qp_pm->Write();
  h2_m2_vs_qp_kp->Write();
  h2_m2_vs_qp_km->Write();
  h2_m2_vs_qp_pr->Write();
  h2_m2_vs_qp_de->Write();
  h2_m2_vs_qp_tr->Write();

  h2_pT_vs_yCM_pp->Write();
  h2_pT_vs_yCM_pm->Write();
  h2_pT_vs_yCM_kp->Write();
  h2_pT_vs_yCM_km->Write();
  h2_pT_vs_yCM_pr->Write();
  h2_pT_vs_yCM_de->Write();
  h2_pT_vs_yCM_tr->Write();


  // Display total allocated memory

  int who = RUSAGE_SELF;
  struct rusage usage;
  int ret;  
  ret = getrusage(who, &usage);

  if (ret == 0) { std::cout << "Memory usage: " << usage.ru_maxrss / 1000 << " MB" << std::endl; }
  else { std::cout << "Could not retrieve memory usage!" << std::endl; }


  outputFile->cd();
  outputFile->Write();

  if (RUN_ITERATION == 0 || RUN_ITERATION == 1)
    {
      correctionOutputFile->cd();

      p_sinAvgsTpc   ->Write();
      p_cosAvgsTpc   ->Write();
      p_sinAvgsTpcA  ->Write();
      p_cosAvgsTpcA  ->Write();
      p_sinAvgsTpcB  ->Write();
      p_cosAvgsTpcB  ->Write();
      p_sinAvgsEpd   ->Write();
      p_cosAvgsEpd   ->Write();
      p_sinAvgsEpdA  ->Write();
      p_cosAvgsEpdA  ->Write();
      p_sinAvgsEpdB  ->Write();
      p_cosAvgsEpdB  ->Write();
      h_XnTpc        ->Write();
      h_YnTpc        ->Write();
      h_XnTpcA       ->Write();
      h_YnTpcA       ->Write();
      h_XnTpcB       ->Write();
      h_YnTpcB       ->Write();
      h_XnEpd        ->Write();
      h_YnEpd        ->Write();
      h_XnEpdA       ->Write();
      h_YnEpdA       ->Write();
      h_XnEpdB       ->Write();
      h_YnEpdB       ->Write();

      gROOT->GetListOfFiles()->Remove(correctionOutputFile);
      correctionOutputFile->Close();
    }

  gROOT->GetListOfFiles()->Remove(outputFile);
  outputFile->Close();

  if (RUN_ITERATION == 1 || RUN_ITERATION == 2)
    {
      gROOT->GetListOfFiles()->Remove(correctionInputFile);
      correctionInputFile->Close();
    }

  std::cout << "Done!" << std::endl;

  ret = getrusage(who, &usage);

  if (ret == 0) { std::cout << "Memory usage: " << usage.ru_maxrss / 1000 << " MB" << std::endl; }
  else { std::cout << "Could not retrieve memory usage!" << std::endl; }

  stopWatch->Stop();
  stopWatch->Print();
  delete stopWatch;
}//End main()
