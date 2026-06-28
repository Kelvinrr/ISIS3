/** This is free and unencumbered software released into the public domain.

The authors of ISIS do not claim copyright on the contents of this file.
For more details about the LICENSE terms and the AUTHORS, you will
find files of those names at the top level of this repository. **/

/* SPDX-License-Identifier: CC0-1.0 */

#include "Isis.h"

#include <QString>

#include "Cube.h"
#include "IString.h"
#include "FileName.h"
#include "ProcessImportPds.h"
#include "Pvl.h"
#include "PvlContainer.h"
#include "PvlKeyword.h"
#include "PvlGroup.h"
#include "Table.h"
#include "UserInterface.h"

using namespace Isis;
using namespace std;

void addTableKeywords(PvlObject &tableLabel, PvlObject pdsTableLabel);

void IsisMain() {
  // Get user interface
  UserInterface &ui = Application::GetUserInterface();
  // Copy the input image to specified output cube in BSQ format
  FileName from = ui.GetFileName("FROM");
  QString pdsLabelFile = from.expanded();
  Pvl pdsLabelPvl;
  ProcessImportPds p;
  p.SetPdsFile(pdsLabelFile, "", pdsLabelPvl);

  QString instId = pdsLabelPvl["INSTRUMENT_ID"][0];
  if(instId != "HIRISE_IDEAL_CAMERA") {
    QString msg = "Invalid PDS label [" + from.expanded() + "]. The PDS product"
                  " must be from an Ideal camera model derived from a HiRISE"
                  " image. The INSTRUMENT_ID = [" + instId + "] is unsupported"
                  " by pds2hideal.";
    throw IException(IException::Io, msg, _FILEINFO_);
  }

  Cube *outputCube = p.SetOutputCube("TO");
  Pvl otherGroups;
  // translate the band bin and archive groups to this pvl
  p.TranslatePdsLabels(otherGroups);

  // Import the requested tables
  std::vector<QString> tables = {"INSTRUMENT_POINTING_TABLE",
                                 "INSTRUMENT_POSITION_TABLE",
                                 "SUN_POSITION_TABLE",
                                 "BODY_ROTATION_TABLE"};
  for (QString table : tables) {
    Table &importedTable = p.ImportTable(table);
    PvlObject &isisTableLabel = importedTable.Label();
    PvlObject pdsTableLabel = pdsLabelPvl.findObject(table);
    addTableKeywords(isisTableLabel, pdsTableLabel);
  }

  p.StartProcess();

  // add translated values from band bin and archive groups to the output cube
  outputCube->putGroup(otherGroups.findGroup("BandBin"));
  outputCube->putGroup(otherGroups.findGroup("Archive"));

  PvlGroup kernelGroup("Kernels");
  kernelGroup += PvlKeyword("NaifIkCode", toString(-74699));
  kernelGroup += PvlKeyword("TargetPosition", "Table");
  kernelGroup += PvlKeyword("InstrumentPointing", "Table");
  kernelGroup += PvlKeyword("InstrumentPosition", "Table");
  QString shapeModelPath = ui.GetString("SHAPEMODELPATH");
  if (!shapeModelPath.endsWith('/')) {
    shapeModelPath += "/";
  }
  QString shapeModelValue = shapeModelPath + pdsLabelPvl["SHAPE_MODEL"][0];
  kernelGroup += PvlKeyword("ShapeModel", shapeModelValue);
  outputCube->putGroup(kernelGroup);


  Pvl *isisLabel = outputCube->label();
  PvlToPvlTranslationManager labelXlater(pdsLabelPvl,
                                 "$ISISROOT/appdata/translations/MroHiriseIdealPdsImportLabel.trn");
  labelXlater.Auto(*isisLabel);

  PvlObject &naifKeywords = isisLabel->findObject("NaifKeywords");
  PvlKeyword bodyRadii("BODY499_RADII");
  bodyRadii.addValue(QString(pdsLabelPvl["A_AXIS_RADIUS"]));
  bodyRadii.addValue(QString(pdsLabelPvl["B_AXIS_RADIUS"]));
  bodyRadii.addValue(QString(pdsLabelPvl["C_AXIS_RADIUS"]));
  naifKeywords += bodyRadii;

  PvlObject &isisCubeObject = isisLabel->findObject("IsisCube");
  // Compute and add SOFTWARE_NAME to the Archive Group
  QString sfname = "Isis " + Application::Version() + " " +
            Application::GetUserInterface().ProgramName();
  PvlGroup &archiveGroup = isisCubeObject.findGroup("Archive");
  archiveGroup += PvlKeyword("SOFTWARE_NAME", sfname);

  PvlObject &pdsImageObj = pdsLabelPvl.findObject("IMAGE");
  double samples = double(pdsImageObj["LINE_SAMPLES"]);
  double lines = double(pdsImageObj["LINES"]);
  double firstSamp = double(pdsImageObj["FIRST_LINE_SAMPLE"]);
  double firstLine = double(pdsImageObj["FIRST_LINE"]);
  double sourceLines = double(pdsImageObj["SOURCE_LINES"]);
  double sourceSamps = double(pdsImageObj["SOURCE_LINE_SAMPLES"]);
  if (sourceLines != lines) {
    // this image is cropped, create an AlphaCube group
    PvlGroup alphaCube("AlphaCube");
    alphaCube += PvlKeyword("AlphaSamples", toString(sourceSamps));
    alphaCube += PvlKeyword("AlphaLines", toString(sourceLines));
    alphaCube += PvlKeyword("AlphaStartingSample", toString(firstSamp));
    alphaCube += PvlKeyword("AlphaEndingSample", toString((double) firstSamp + samples));
    alphaCube += PvlKeyword("AlphaStartingLine", toString(firstLine));
    alphaCube += PvlKeyword("AlphaEndingLine", toString((double) firstLine + lines));
    alphaCube += PvlKeyword("BetaSamples", toString(samples));
    alphaCube += PvlKeyword("BetaLines", toString(lines));
    isisCubeObject += alphaCube;
  }

  p.EndProcess();
}

/**
 * This method will add the appropriate keywords from the TABLE object of the
 * input labels to the Table object in output Isis labels.
 *
 * @param tableLabel Reference to the output file's table label
 * @param pdsTableLabel A PvlObject containing the input pds file's table label.
 *
 */
void addTableKeywords(PvlObject &tableLabel, PvlObject pdsTableLabel) {
  QStringList ingnoredKeywords = {"INTERCHANGE_FORMAT", "ROWS", "COLUMNS", "ROW_BYTES", "ROW_SUFFIX_BYTES"};
  for (PvlContainer::ConstPvlKeywordIterator keywordIt = pdsTableLabel.begin(); keywordIt != pdsTableLabel.end(); keywordIt++) {
    if (ingnoredKeywords.contains(keywordIt->name())) {
      continue;
    }
    PvlKeyword newKeyword(*keywordIt);
    QString keywordName = newKeyword.name();

    QString upperCamelCaseName;
    QStringList parts = keywordName.split('_', Qt::SkipEmptyParts);
    for (QString part : parts) {
      upperCamelCaseName += part.at(0).toUpper() + part.mid(1).toLower();
    }

    newKeyword.setName(upperCamelCaseName);
    tableLabel += newKeyword;
  }
}
