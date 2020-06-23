#include <QDebug>

#include <iomanip>
#include <iostream>

#include "Hyb2OncCamera.h"
#include "Camera.h"
#include "CameraFactory.h"
#include "IException.h"
#include "iTime.h"
#include "Preference.h"
#include "Pvl.h"
#include "TestUtilities.h"
#include "PvlGroup.h"
#include "Fixtures.h"

#include "gmock/gmock.h"

using namespace std;
using namespace Isis;

void testCamera(Cube &c, double knownLat, double knownLon,
                double s1, double l1, 
                double s2, double l2, 
                double s3, double l3, 
                double s4, double l4);
void testLineSamp(Camera *cam, double sample, double line);

class Hayabusa2Cube : public DefaultCube {
  protected:
    void setInstrument(QString ikid, QString instrumentId, QString spacecraftName) {
      PvlGroup &kernels = testCube->label()->findObject("IsisCube").findGroup("Kernels");
      kernels.findKeyword("NaifFrameCode").setValue(ikid);    
      
      PvlGroup &inst = testCube->label()->findObject("IsisCube").findGroup("Instrument");
      inst.findKeyword("InstrumentId").setValue(instrumentId);
      inst.findKeyword("SpacecraftName").setValue(spacecraftName);
      PvlKeyword startcc("SpacecraftClockStartCount", "33322515");
      PvlKeyword stopcc("SpaceCraftClockStopCount", "33322515");
      PvlKeyword binning("Binning", "1");
      inst += startcc;
      inst += stopcc; 
      inst += binning; 

      PvlObject &naifKeywords = testCube->label()->findObject("NaifKeywords");
      
      json nk; 
      nk["INS"+ikid.toStdString()+"_FOCAL_LENGTH"] = 10.44;
      nk["INS"+ikid.toStdString()+"_PIXEL_PITCH"] = 0.013;
      nk["INS"+ikid.toStdString()+"_TRANSX"] = {0.0, 0.013, 0.0};
      nk["INS"+ikid.toStdString()+"_TRANSY"] = {0.0, 0.0, 0.013};
      nk["INS"+ikid.toStdString()+"_ITRANSS"] = {0.0, 76.923076923077, 0.0};
      nk["INS"+ikid.toStdString()+"_ITRANSL"] = {0.0, 0.0, 76.923076923077};
      nk["INS"+ikid.toStdString()+"_BORESIGHT_LINE"] = 490.5;
      nk["INS"+ikid.toStdString()+"_BORESIGHT_SAMPLE"] = 512.5;
      nk["INS"+ikid.toStdString()+"_OD_K"] = {1.014, 2.933e-07, -1.384e-13};
      nk["BODY499_RADII"] = {3396.19, 3396.19, 3376.2};
      nk["CLOCK_ET-37_33322515_COMPUTED"] = "8ed6ae8930f3bd41";
      nk["BODY_CODE"] = 499;
      nk["BODY_FRAME_CODE"] = 10014; 
      PvlObject newNaifKeywords("NaifKeywords", nk);
      naifKeywords = newNaifKeywords; 

      QString fileName = testCube->fileName();
      // need to remove old camera pointer 
      delete testCube;
      // This is now a Hayabusa cube
      testCube = new Cube(fileName, "rw");
    }
};


TEST_F(Hayabusa2Cube, Hayabusa2OncCameraW1CameraTest) {
  setInstrument("-37110", "ONC-W1", "HAYABUSA-2");
  Camera *cam = testCube->camera();
  EXPECT_EQ(cam->CkFrameId(), -37000);
  EXPECT_EQ(cam->CkReferenceId(), 1);
  EXPECT_EQ(cam->SpkTargetId(), -37);
  EXPECT_EQ(cam->SpkReferenceId(), 1);
   
  // Test name methods
  EXPECT_PRED_FORMAT2(AssertQStringsEqual, cam->spacecraftNameLong(), "Hayabusa2");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->spacecraftNameShort(), "Hayabusa2");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->instrumentNameLong(), "Optical Navigation Camera - W1 Camera");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->instrumentNameShort(), "ONC-W1");

  testCamera(*testCube, 11.215766294778371, 257.19997053715304, 51.0, 42.0, 173.0, 21.0, 54.0, 149.0, 174.0, 155.0);
}


TEST_F(Hayabusa2Cube, Hayabusa2OncCameraW2CameraTest) {
  setInstrument("-37120", "ONC-W2", "HAYABUSA-2");
   
  Camera *cam = testCube->camera();
  EXPECT_EQ(cam->CkFrameId(), -37000);
  EXPECT_EQ(cam->CkReferenceId(), 1);
  EXPECT_EQ(cam->SpkTargetId(), -37);
  EXPECT_EQ(cam->SpkReferenceId(), 1);
   
  // Test name methods
  EXPECT_PRED_FORMAT2(AssertQStringsEqual, cam->spacecraftNameLong(), "Hayabusa2");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->spacecraftNameShort(), "Hayabusa2");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->instrumentNameLong(), "Optical Navigation Camera - W2 Camera");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->instrumentNameShort(), "ONC-W2");

  testCamera(*testCube, 11.215766294778371, 257.19997053715304, 51.0, 42.0, 173.0, 21.0, 54.0, 149.0, 174.0, 155.0);

  
}

TEST_F(Hayabusa2Cube, Hayabusa2OncCameraTelecopicCameraTest) {
  setInstrument("-37100", "ONC-T", "HAYABUSA-2");

  Camera *cam = testCube->camera(); 
  EXPECT_EQ(cam->CkFrameId(), -37000);
  EXPECT_EQ(cam->CkReferenceId(), 1);
  EXPECT_EQ(cam->SpkTargetId(), -37);
  EXPECT_EQ(cam->SpkReferenceId(), 1);
   
  // Test name methods
  EXPECT_PRED_FORMAT2(AssertQStringsEqual, cam->spacecraftNameLong(), "Hayabusa2");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->spacecraftNameShort(), "Hayabusa2");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->instrumentNameLong(), "Optical Navigation Camera - Telescopic Camera");
  EXPECT_PRED_FORMAT2(AssertQStringsEqual,cam->instrumentNameShort(), "ONC-T");

  testCamera(*testCube, 11.215766294778371, 257.19997053715304, 51.0, 42.0, 173.0, 21.0, 54.0, 149.0, 174.0, 155.0);
}

void testCamera(Cube &c,
                double knownLat, double knownLon,
                double s1, double l1, 
                double s2, double l2, 
                double s3, double l3, 
                double s4, double l4) {
  Camera *cam = (Hyb2OncCamera *) CameraFactory::Create(c);
  
  // Test Shutter Open/Close 
  const PvlGroup &inst = c.label()->findGroup("Instrument", Pvl::Traverse);
  double exposureDuration = ((double) inst["ExposureDuration"])/1000;
  QString stime = inst["StartTime"];
  double et; // StartTime keyword is the center exposure time
  str2et_c(stime.toLatin1().data(), &et);
  pair <iTime, iTime> shuttertimes = cam->ShutterOpenCloseTimes(et, exposureDuration);
  
  EXPECT_EQ(shuttertimes.first.Et(), -709401200.8161447);
  EXPECT_EQ(shuttertimes.second.Et(), -709401200.8161362);

  // Test all four corners to make sure the conversions are right
  testLineSamp(cam, s1, l1);
  testLineSamp(cam, s2, l2);
  testLineSamp(cam, s3, l3);
  testLineSamp(cam, s4, l4);
  
  if (!cam->SetImage((cam->Samples()/2.0), (cam->Lines()/2.0))) {
    throw IException(IException::Unknown, "ERROR setting image to known position.", _FILEINFO_);
  }

  EXPECT_NEAR(cam->UniversalLatitude(), knownLat,  1E-10);
  EXPECT_NEAR(cam->UniversalLongitude(), knownLon,  1E-10);
  testLineSamp( cam, (cam->Samples()/2.0), (cam->Lines()/2.0) );
}


void testLineSamp(Camera *cam, double sample, double line) {
  bool success = cam->SetImage(sample, line);

  if (success) {
    double lat = cam->UniversalLatitude();
    double lon = cam->UniversalLongitude();
    success = cam->SetUniversalGround(lat, lon);
  }
  else {
    FAIL() << "Failed to set sample/line (Line: " << line << ", Sample: " << sample << ")." ;
  }

  if (success) {
    EXPECT_NEAR(sample, cam->Sample(), .001); 
    EXPECT_NEAR(line, cam->Line(), .001);
  }
}
