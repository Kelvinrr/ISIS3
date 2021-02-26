#include "Fixtures.h"
#include "Pvl.h"
#include "PvlGroup.h"
#include "TestUtilities.h"
#include "Histogram.h"

#include "moccal.h"

#include "gtest/gtest.h"

using namespace Isis;

static QString APP_XML = FileName("$ISISROOT/bin/xml/moccal.xml").expanded();

TEST_F(MgsMocCube, FunctionalTestMroMoccalDefault) {
  QString outCubeFileName = tempDir.path() + "/outTemp.cub";
  QVector<QString> args = {"to="+outCubeFileName};
  
  UserInterface options(APP_XML, args);

  try {
    moccal(testCube.get(), options);
  }
  catch (IException &e) {
    FAIL() << "Unable to open image: " << e.what() << std::endl;
  }
  
  Cube oCube(outCubeFileName, "r");

  Histogram *oCubeStats = oCube.histogram();

  EXPECT_DOUBLE_EQ(oCubeStats->Average(), 0.056909484090283513);
  EXPECT_DOUBLE_EQ(oCubeStats->Sum(), 22.763793636113405);
  EXPECT_DOUBLE_EQ(oCubeStats->ValidPixels(), 400);
  EXPECT_DOUBLE_EQ(oCubeStats->StandardDeviation(), 0.0021719888294085255);
}


TEST_F(MgsMocCube, FunctionalTestMroMoccalIofFalse) {
  QString outCubeFileName = tempDir.path() + "/outTemp.cub";
  QVector<QString> args = {"to="+outCubeFileName, "iof=False"};
  
  UserInterface options(APP_XML, args);

  try {
    moccal(testCube.get(), options);
  }
  catch (IException &e) {
    FAIL() << "Unable to open image: " << e.what() << std::endl;
  }
  
  Cube oCube(outCubeFileName, "r");

  Histogram *oCubeStats = oCube.histogram();

  EXPECT_DOUBLE_EQ(oCubeStats->Average(), 0.11209752136841417);
  EXPECT_DOUBLE_EQ(oCubeStats->Sum(), 44.839008547365665);
  EXPECT_DOUBLE_EQ(oCubeStats->ValidPixels(), 400);
  EXPECT_DOUBLE_EQ(oCubeStats->StandardDeviation(), 0.004278277553211739);
}


TEST_F(MgsMocCube, FunctionalTestMroMoccalNullwagoTrue) {
  QString outCubeFileName = tempDir.path() + "/outTemp.cub";
  QVector<QString> args = {"to="+outCubeFileName, "nullwag=True"};
  
  UserInterface options(APP_XML, args);

  try {
    moccal(testCube.get(), options);
  }
  catch (IException &e) {
    FAIL() << "Unable to open image: " << e.what() << std::endl;
  }
  
  Cube oCube(outCubeFileName, "r");

  Histogram *oCubeStats = oCube.histogram();

  EXPECT_DOUBLE_EQ(oCubeStats->Average(), 0.056909484090283513);
  EXPECT_DOUBLE_EQ(oCubeStats->Sum(), 22.763793636113405);
  EXPECT_DOUBLE_EQ(oCubeStats->ValidPixels(), 400);
  EXPECT_DOUBLE_EQ(oCubeStats->StandardDeviation(), 0.0021719888294085255);
}


TEST_F(MgsMocCube, FunctionalTestMroMoccalCameraComparison) {
  QString outCubeFileNameCam = tempDir.path() + "/outTemp.cub";
  QVector<QString> args = {"to="+outCubeFileNameCam};

  UserInterface options(APP_XML, args);

  try {
    moccal(testCube.get(), options);
  }
  catch (IException &e) {
    FAIL() << "Unable to open image: " << e.what() << std::endl;
  }

  // force camera to not construct
  Pvl *lab = testCube->label(); 
  lab->deleteObject("NaifKeywords");

  QString outCubeFileNameNoCam = tempDir.path() + "/outTempNoCam.cub";
  args = {"to="+outCubeFileNameNoCam};

  try {
    moccal(testCube.get(), options);
  }
  catch (IException &e) {
    FAIL() << "Unable to open image: " << e.what() << std::endl;
  }

  Cube oNoCamCube(outCubeFileNameCam, "r");
  Cube oCamCube(outCubeFileNameCam, "r");

  PvlGroup noCamLab = oNoCamCube.label()->findObject("IsisCube").findGroup("Radiometry"); 
  PvlGroup camLab = oCamCube.label()->findObject("IsisCube").findGroup("Radiometry");

  EXPECT_DOUBLE_EQ((double)noCamLab.findKeyword("iof"), 
                   (double)camLab.findKeyword("iof"));

  EXPECT_DOUBLE_EQ((double)noCamLab.findKeyword("s"), 
                   (double)camLab.findKeyword("s"));
}