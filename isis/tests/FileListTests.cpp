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
