#include <gtest/gtest.h>
#include <iostream>
#include "FileList.h"
#include "FileName.h"
#include "IException.h"
#include "TestUtilities.h"

#include "cpl_vsi.h"

TEST(FileList, NonExistantFileConstructor)
{
  try
  {
    Isis::FileList fl1(Isis::FileName("FakeFile"));
  }
  catch(Isis::IException &e)
  {
    EXPECT_THAT(e.what(), testing::HasSubstr("Unable to open [FakeFile]"));
  }
  catch(...)
  {
    FAIL() << "Expected an IException\"Unable to open [FakeFile]\"";
  }
}

TEST(FileList, FileNameConstructor)
{
  std::istringstream input(
  "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.cpp\n"
  "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.h\n"
  "#Comment\n"
  "unitTest.cpp\n"
  ">This will not be comment ignored\n"
  "\n"
  "^is a blank line, this line will not be ignored as a comment\n"
  "  Makefile\n"
  "  //Testing comment with prepended spaces\n"
  "\n"
  "#Above and below are for testing multiple blank lines\n"
  "\n"
  "\n"
  "FileList.h\n");
  std::ostringstream output;
  std::string expectedOutput = "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.cpp\n"
     "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.h\n"
     "unitTest.cpp\n>This\n^is\nMakefile\nFileList.h\n";
  Isis::FileList fl1(input);
  fl1.write(output);
  EXPECT_STREQ(expectedOutput.c_str(), output.str().c_str());
}

// Write a FileList to a GDAL VSI path (/vsimem) and read it back, confirming
// VSI read+write works and that VSI-style entries (incl. ones containing "://")
// are not misparsed as comments.
TEST(FileList, VsiReadWriteRoundTrip)
{
  std::istringstream input(
    "/vsicurl/https://example.com/data/cube1.cub\n"
    "/vsis3/bucket/cube2.cub\n"
    "local/relative/cube3.cub\n");
  Isis::FileList toWrite(input);

  const QString vsiPath = "/vsimem/filelist_test.lis";
  ASSERT_NO_THROW(toWrite.write(Isis::FileName(vsiPath)));

  Isis::FileList readBack;
  ASSERT_NO_THROW(readBack.read(Isis::FileName(vsiPath)));

  ASSERT_EQ(readBack.size(), 3);
  EXPECT_EQ(readBack[0].toString(), QString("/vsicurl/https://example.com/data/cube1.cub"));
  EXPECT_EQ(readBack[1].toString(), QString("/vsis3/bucket/cube2.cub"));
  EXPECT_EQ(readBack[2].toString(), QString("local/relative/cube3.cub"));

  VSIUnlink(vsiPath.toUtf8().constData());
}

// Reading a nonexistent VSI path throws an IException rather than crashing.
TEST(FileList, VsiMissingFileThrows)
{
  Isis::FileList fl;
  EXPECT_THROW(fl.read(Isis::FileName("/vsimem/does_not_exist.lis")), Isis::IException);
}

TEST(FileList, FileNameNoNewLine)
{
  std::istringstream input(
  "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.cpp\n"
  "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.h");
  std::ostringstream output;
  std::string expectedOutput = "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.cpp\n"
     "/usgs/pkgs/isis3/isis/src/base/objs/FileList/FileList.h\n";
  Isis::FileList fl1(input);
  fl1.write(output);
  EXPECT_STREQ(expectedOutput.c_str(), output.str().c_str());
}
