#include <QtMath>

#include "Pvl.h"
#include "PvlGroup.h"
#include "ControlNet.h"
#include "Statistics.h"
#include "CSVReader.h"

#include "jigsaw.h"

#include "TestUtilities.h"
#include "Fixtures.h"
#include "gmock/gmock.h"

using namespace Isis;
using namespace testing; 


static QString APP_XML = FileName("$ISISROOT/bin/xml/jigsaw.xml").expanded();

TEST_F(ObservationPair, FunctionalTestJigsawCamSolveAll) {
  // delete to remove old camera for when cam is updated
  delete cubeL;
  delete cubeR;      
  
  QTemporaryDir prefix;
  QString outCnetFileName = prefix.path() + "/outTemp.net";
  QVector<QString> args = {"fromlist="+cubeListFile, "cnet="+cnetPath, "onet="+outCnetFileName, 
                           "observations=yes", "update=yes", "Cksolvedegree=3", 
                           "Camsolve=all", "twist=no", "Spsolve=none", "Radius=no", "imagescsv=on", "file_prefix="+prefix.path()+"/"};

  UserInterface options(APP_XML, args);
  
  Pvl log; 
  try {
    jigsaw(options, &log);
  }
  catch (IException &e) {
    FAIL() << "Unable to bundle: " << e.what() << std::endl;
  }
  
  // images were updated 
  cubeL = new Cube(cubeLPath, "r");
  cubeR = new Cube(cubeRPath, "r");
  
  ControlNet oNet;
  oNet.ReadControl(outCnetFileName);

  EXPECT_NEAR(oNet.AverageResidual(), 0.123132, 0.00001); 
  EXPECT_NEAR(oNet.GetMaximumResidual(), 0.379967, 0.00001);
  ASSERT_EQ(oNet.GetNumIgnoredMeasures(), 0);
  ASSERT_EQ(oNet.GetNumValidPoints(), 46);
  
  QList<ControlPoint*> points = oNet.GetPoints(); 
  
  Statistics xstats;
  Statistics ystats;
  Statistics zstats;

  for (int i = 0; i < points.size(); i++) { 
      xstats.AddData(points.at(i)->GetAdjustedSurfacePoint().GetX().kilometers());
      ystats.AddData(points.at(i)->GetAdjustedSurfacePoint().GetY().kilometers());
      zstats.AddData(points.at(i)->GetAdjustedSurfacePoint().GetZ().kilometers());
  }
  
  EXPECT_NEAR(xstats.Average(),           1556.64806314499741, 0.00001);
  EXPECT_NEAR(xstats.StandardDeviation(), 10.663072757957551,  0.00001);
  EXPECT_NEAR(xstats.Minimum(),           1540.43360835455860, 0.00001);
  EXPECT_NEAR(xstats.Maximum(),           1574.6528854394717,  0.00001);

  EXPECT_NEAR(ystats.Average(),           98.326253648503553, 0.00001);
  EXPECT_NEAR(ystats.StandardDeviation(), 1.3218686492693708, 0.00001);
  EXPECT_NEAR(ystats.Minimum(),           96.795117686735381, 0.00001);
  EXPECT_NEAR(ystats.Maximum(),           100.04990583087032, 0.00001);

  EXPECT_NEAR(zstats.Average(),           763.0309515939565,  0.00001);
  EXPECT_NEAR(zstats.StandardDeviation(), 19.783664466904419, 0.00001);
  EXPECT_NEAR(zstats.Minimum(),           728.82827218510067, 0.00001);
  EXPECT_NEAR(zstats.Maximum(),           793.9672179283682,  0.00001); 

  Camera *cam = cubeL->camera(); 
  SpiceRotation *rot = cam->instrumentRotation();
  std::vector<double> a1; 
  std::vector<double> a2; 
  std::vector<double> a3;

  rot->GetPolynomial(a1, a2, a3); 
  
  EXPECT_NEAR(a1.at(0), 2.16338,    0.0001);
  EXPECT_NEAR(a1.at(1), -0.0264475, 0.0001);
  EXPECT_NEAR(a1.at(2), 0.00469675, 0.0001);
  EXPECT_NEAR(a1.at(3), 0.0210955,  0.0001);
  
  EXPECT_NEAR(a2.at(0), 1.83011,     0.0001);
  EXPECT_NEAR(a2.at(1), -0.0244244,  0.0001);
  EXPECT_NEAR(a2.at(2), -0.00456569, 0.0001);
  EXPECT_NEAR(a2.at(3), 0.00637157,  0.0001);

  QFile file(prefix.path() + "/bundleout_images.csv");
  if (!file.open(QIODevice::ReadOnly)) {
    FAIL() << file.errorString().toStdString();
  }
  
  // skip the first two lines, we don't want to compare the header. 
  file.readLine();
  file.readLine(); 

  QString line = file.readLine();
  QStringList elems = line.split(","); 
  
  // RA(t0) final
  EXPECT_NEAR(elems.at(21).toDouble(), 123.9524918, 0.00001); 
  // RA(t1) final
  EXPECT_NEAR(elems.at(26).toDouble(), -1.51532975, 0.00001); 
  // RA(t2) final
  EXPECT_NEAR(elems.at(31).toDouble(), 0.2691039,   0.00001); 
  // RA(t3) final
  EXPECT_NEAR(elems.at(36).toDouble(), 1.208684781, 0.00001); 

  // DEC(t0) final
  EXPECT_NEAR(elems.at(41).toDouble(), 104.8575294,       0.00001); 
  // DEC(t1) final
  EXPECT_NEAR(elems.at(46).toDouble(), -1.399416621,      0.00001); 
  // DEC(t2) final
  EXPECT_NEAR(elems.at(51).toDouble(), -0.26159502200533, 0.00001); 
  // DEC(t3) final
  EXPECT_NEAR(elems.at(56).toDouble(), 0.365064224,       0.00001); 
  
  
  line = file.readLine();
  elems = line.split(","); 
  
  // RA(t0) final
  EXPECT_NEAR(elems.at(21).toDouble(), 121.4164029, 0.00001); 
  // RA(t1) final
  EXPECT_NEAR(elems.at(26).toDouble(), -1.510464718, 0.00001); 
  // RA(t2) final
  EXPECT_NEAR(elems.at(31).toDouble(), 0.253046705,   0.00001); 
  // RA(t3) final
  EXPECT_NEAR(elems.at(36).toDouble(), 1.203832854, 0.00001); 

  // DEC(t0) final
  EXPECT_NEAR(elems.at(41).toDouble(), 106.11241033284,      0.00001); 
  // DEC(t1) final
  EXPECT_NEAR(elems.at(46).toDouble(), -1.4160602752902001,  0.00001); 
  // DEC(t2) final
  EXPECT_NEAR(elems.at(51).toDouble(), -0.26704142,          0.00001); 
  // DEC(t3) final
  EXPECT_NEAR(elems.at(56).toDouble(), 0.365717165,          0.00001); 
  
}


TEST_F(ApolloNetwork, FunctionalTestJigsawHeldList) {
  QTemporaryDir prefix;
  
  QString heldlistpath = prefix.path() + "/heldlist.lis"; 
  FileList heldList; 
  heldList.append(cube6->fileName());
  heldList.write(heldlistpath); 

  QString outCnetFileName = prefix.path() + "/outTemp.net";
  QVector<QString> args = {"fromlist="+cubeListFile, "cnet="+controlNetPath, "onet="+outCnetFileName, "heldlist="+heldlistpath,  
                           "radius=yes", "errorpropagation=yes", "spsolve=position", "Spacecraft_position_sigma=1000", 
                           "Residuals_csv=off", "Camsolve=angles", "Twist=yes", "Camera_angles_sigma=2", 
                           "Output_csv=off", "imagescsv=on", "file_prefix="+prefix.path()+"/"};

  UserInterface options(APP_XML, args);
  
  Pvl log; 
  
  try {
    jigsaw(options, &log);
  }
  catch (IException &e) {
    FAIL() << "Unable to bundle: " << e.what() << std::endl;
  }

  CSVReader::CSVAxis csvLine;
  CSVReader header = CSVReader(prefix.path()+"/bundleout_images.csv",
                               false, 0, ',', false, true);

  csvLine = header.getRow(7);

  // assert corrections are very small 
  // X Correction
  EXPECT_LE(std::abs(csvLine[5].toDouble()), 1e-10); 
  // Y Correction
  EXPECT_LE(std::abs(csvLine[10].toDouble()), 1e-10); 
  // Z Correction
  EXPECT_LE(std::abs(csvLine[15].toDouble()), 1e-10); 
  // RA Correction
  EXPECT_LE(std::abs(csvLine[20].toDouble()), 1e-10); 
  // DEC Correction
  EXPECT_LE(std::abs(csvLine[25].toDouble()), 1e-10); 
  // TWIST Correction
  EXPECT_LE(std::abs(csvLine[30].toDouble()), 1e-10); 
}


TEST_F(ApolloNetwork, FunctionalTestJigsawMEstimator) {
  QTemporaryDir prefix;
  QString newNetworkPath = prefix.path()+"/badMeasures.net";
  
  QVector<QString> pid = {"AS15_000031985", 
                          "AS15_000033079", 
                          "AS15_SocetPAN_03", 
                          "AS15_Tie03"};
 
  QVector<QString> mid = {"APOLLO15/METRIC/1971-07-31T14:01:40.346", 
                          "APOLLO15/METRIC/1971-07-31T14:02:27.179", 
                          "APOLLO15/METRIC/1971-07-31T14:02:03.751", 
                          "APOLLO15/METRIC/1971-07-31T14:00:53.547"};
  
  for (int i = 0; i < pid.size(); i++) {
    // grab random points and add error to a single measure 
    ControlPoint *point = inputNet->GetPoint(pid[i]);
    ControlMeasure *measure = point->GetMeasure(mid[i]);
    measure->SetCoordinate(measure->GetLine()+50, measure->GetLine()+50); 
  }

  inputNet->Write(newNetworkPath); 
  
  QString outCnetFileName = prefix.path() + "/outTemp.net";
  QVector<QString> args = {"fromlist="+cubeListFile, "cnet="+newNetworkPath, "onet="+outCnetFileName,
                           "Radius=yes", "Errorpropagation=yes", "Spsolve=position","Spacecraft_position_sigma=1000.0",
                           "Camsolve=angles", "twist=yes", "Camera_angles_sigma=2", 
                           "Model1=huber", "Max_model1_c_quantile=0.6", "Model2=chen", "Max_model2_c_quantile=0.98", "Sigma0=1e-3", 
                           "bundleout_txt=yes", "Output_csv=on", "imagescsv=on", "file_prefix="+prefix.path()+"/"};

  UserInterface options(APP_XML, args);

  Pvl log; 
  try {
    jigsaw(options, &log);
  }
  catch (IException &e) {
    FAIL() << "Unable to bundle: " << e.what() << std::endl;
  }

  CSVReader::CSVAxis csvLine;
  CSVReader header = CSVReader(prefix.path()+"/bundleout_images.csv",
                               false, 0, ',', false, true);

  ControlNet onet; 
  onet.ReadControl(outCnetFileName);
 
  QVector<double> presiduals = {};
  QVector<QVector<double>> mresiduals = {{1.27975, 1.54281, 1.8778, 1.30159}, 
                                         {2.25115, 2.33559, 0.547574, 3.16777}, 
                                         {1.15396, 0.69243, 1.03005, 0.848934}, 
                                         {2.24641, 4.39168, 0.560941, 2.844}}; 

  for (int i = 0; i < pid.size(); i++) {
    ControlPoint *point = inputNet->GetPoint(pid[i]);
    QList<ControlMeasure*> measures = point->getMeasures();
    for (int j = 0; j < measures.size(); j++ ) {
      EXPECT_NEAR(measures.at(j)->GetResidualMagnitude(), mresiduals[i][j], 0.0001); 
    }
  }

  QFile bo(prefix.path()+"/bundleout.txt");
  QString contents; 
  if (bo.open(QIODevice::ReadOnly)) {
    contents = bo.read(bo.size()); 
  }
  else { 
    FAIL() << "Failed to open bundleout.txt" << std::endl;
  }

  QStringList lines = contents.split("\n");

  EXPECT_THAT(lines[31].toStdString(), HasSubstr("Tier 0 Enabled: TRUE"));
  EXPECT_THAT(lines[32].toStdString(), HasSubstr("Maximum Likelihood Model: Huber"));
  EXPECT_THAT(lines[33].toStdString(), HasSubstr("Quantile used for tweaking constant: 0.6"));
  EXPECT_THAT(lines[34].toStdString(), HasSubstr("Quantile weighted R^2 Residual value: 0.207"));
  EXPECT_THAT(lines[35].toStdString(), HasSubstr("Approx. weighted Residual cutoff: N/A"));

  EXPECT_THAT(lines[37].toStdString(), HasSubstr("Tier 1 Enabled: TRUE"));
  EXPECT_THAT(lines[38].toStdString(), HasSubstr("Maximum Likelihood Model: Chen"));
  EXPECT_THAT(lines[39].toStdString(), HasSubstr("Quantile used for tweaking constant: 0.98"));
  EXPECT_THAT(lines[40].toStdString(), HasSubstr("Quantile weighted R^2 Residual value: 1.0"));
  EXPECT_THAT(lines[41].toStdString(), HasSubstr("Approx. weighted Residual cutoff: 1.0"));

  EXPECT_THAT(lines[43].toStdString(), HasSubstr(" Tier 2 Enabled: FALSE"));
}


 TEST_F(ObservationPair, FunctionalTestJigsawErrorNoSolve) {
  QTemporaryDir prefix;
  QString outCnetFileName = prefix.path() + "/outTemp.net";
  QVector<QString> args = {"fromlist="+cubeListFile, "cnet="+cnetPath, "onet="+outCnetFileName, 
                           "camsolve=None", "spsolve=None"}; 
  
  UserInterface options(APP_XML, args); 
  Pvl log; 

  try {
    jigsaw(options, &log);
    FAIL() << "Should throw" << std::endl;
  }
  catch (IException &e) {
    EXPECT_THAT(e.what(), HasSubstr("Must either solve for camera pointing or spacecraft position"));
  }
}


TEST_F(ObservationPair, FunctionalTestJigsawErrorTBParamsNoTarget) {
  QTemporaryDir prefix;
  QString outCnetFileName = prefix.path() + "/outTemp.net";
  
  // just use isdPath for a valid PVL file without the wanted groups
  QVector<QString> args = {"fromlist="+cubeListFile, "cnet="+cnetPath, "onet="+outCnetFileName, "SOLVETARGETBODY=TRUE", "tbparameters="+cubeRPath};

  UserInterface options(APP_XML, args);
  
  Pvl log; 
  
  try {
    jigsaw(options, &log);
    FAIL() << "Should throw an exception" << std::endl;
  }
  catch (IException &e) {
    EXPECT_THAT(e.what(), HasSubstr("Input Target parameters file missing main Target object"));
  } 
}


TEST_F(ObservationPair, FunctionalTestJigsawErrorTBParamsNoSolve) {
  QTemporaryDir prefix;
  QString outCnetFileName = prefix.path() + "/outTemp.net";
  
  std::istringstream iss(R"(
    Object = Target
    Group = "NAME"
       Name=Enceladus
    EndGroup
    END_OBJECT
  )"); 
  
  QString tbsolvepath = prefix.path() + "/tbsolve.pvl";
  Pvl tbsolve; 
  iss >> tbsolve; 
  tbsolve.write(tbsolvepath);

  // just use isdPath for a valid PVL file without the wanted groups
  QVector<QString> args = {"fromlist="+cubeListFile, "cnet="+cnetPath, "onet="+outCnetFileName, "SOLVETARGETBODY=TRUE", "tbparameters="+tbsolvepath};

  UserInterface options(APP_XML, args);
  
  Pvl log; 
  
  try {
    jigsaw(options, &log);
    FAIL() << "Should throw an exception" << std::endl;
  }
  catch (IException &e) {
    EXPECT_THAT(e.what(), HasSubstr("Must solve for at least one target body option"));
  } 
}
