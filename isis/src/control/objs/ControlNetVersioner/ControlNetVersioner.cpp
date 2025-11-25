/** This is free and unencumbered software released into the public domain.

The authors of ISIS do not claim copyright on the contents of this file.
For more details about the LICENSE terms and the AUTHORS, you will
find files of those names at the top level of this repository. **/

/* SPDX-License-Identifier: CC0-1.0 */

#include "ControlNetVersioner.h"

#include <boost/numeric/ublas/symmetric.hpp>
#include <boost/numeric/ublas/io.hpp>

#include <QDebug>
#include <QString>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include "ControlNetFileHeaderV0002.pb.h"
#include "ControlNetFileHeaderV0005.pb.h"
#include "ControlNetLogDataProtoV0001.pb.h"
#include "ControlPointFileEntryV0002.pb.h"

#include "ControlMeasure.h"
#include "ControlNet.h"
#include "ControlMeasureLogData.h"
#include "Distance.h"
#include "EndianSwapper.h"
#include "FileName.h"
#include "IException.h"
#include "Latitude.h"
#include "LinearAlgebra.h"
#include "Longitude.h"
#include "NaifStatus.h"
#include "Progress.h"
#include "Pvl.h"
#include "PvlGroup.h"
#include "PvlKeyword.h"
#include "PvlObject.h"
#include "SpecialPixel.h"
#include "SurfacePoint.h"
#include "Target.h"


#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message_lite.h>


using boost::numeric::ublas::symmetric_matrix;
using boost::numeric::ublas::upper;
using namespace google::protobuf::io;
using namespace std;

namespace Isis {

  /**
   * Construct a ControlNetVersioner from a control network. This versioner can only be used to
   * write out the control points in the control network. It is expected that the control points
   * in the control network will not be deleted while the control net versioner persists.
   *
   * @param net A pointer to the network that will be written out.
   */
  ControlNetVersioner::ControlNetVersioner(ControlNet *net)
      : m_ownsPoints(false) {
    // Populate the internal list of points.
    m_points.append( net->GetPoints() );

    ControlNetHeaderV0001 header;

    header.networkID = net->GetNetworkId();
    header.targetName = net->GetTarget();
    header.created = net->CreatedDate();
    header.lastModified = net->GetLastModified();
    header.description = net->Description();
    header.userName = net->GetUserName();
    createHeader(header);
  }


  /**
   * Construct a ControlNetVersioner from a file. The file will be read in and converted into
   * a header object that contains general information about the network and a list of
   * ControlPoints.
   *
   * @param netFile The control network file to read in.
   * @param progress The progress object to track reading points.
   *
   * @see ControlNetVersioner::Read
   */
  ControlNetVersioner::ControlNetVersioner(const FileName netFile, Progress *progress)
      : m_ownsPoints(true) {
    read(netFile, progress);
  }


  /**
   * Destroy a ControlNetVersioner. If the versioner owns the control points stored in it,
   * they will also be deleted.
   */
  ControlNetVersioner::~ControlNetVersioner() {
    if ( m_ownsPoints ) {
      while ( !m_points.isEmpty() ) {
        ControlPoint *unusedPoint = m_points.takeFirst();
        delete unusedPoint;
        unusedPoint = NULL;
      }
    }
  }


  /**
   * Returns the ID for the network.
   *
   * @return @b QString The network ID as a string
   */
  QString ControlNetVersioner::netId() const {
    return m_header.networkID;
  }


  /**
   * Returns the target for the network.
   *
   * @return @b QString The target name as a string
   */
  QString ControlNetVersioner::targetName() const {
    return m_header.targetName;
  }


  /**
   * Returns the date and time that the network was created
   *
   * @return @b QString The date and time the network was created as a string
   */
  QString ControlNetVersioner::creationDate() const {
    return m_header.created;
  }


  /**
   * Returns the date and time of the last modification to the network.
   *
   * @return @b QString The date and time of the last modfication as a string
   */
  QString ControlNetVersioner::lastModificationDate() const {
    return m_header.lastModified;
  }


  /**
   * Returns the network's description.
   *
   * @return @b QString A description of the network.
   */
  QString ControlNetVersioner::description() const {
    return m_header.description;
  }


  /**
   * Returns the name of the last person or program to modify the network.
   *
   * @return @b QString The name of the last person or program to modify the network.
   */
  QString ControlNetVersioner::userName() const {
    return m_header.userName;
  }


  /**
   * Returns the number of points that have been read in or are ready to write out.
   *
   * @return @b int The number of control points stored internally.
   */
  int ControlNetVersioner::numPoints() const {
    return m_points.size();
  }


  /**
   * Returns the first point stored in the versioner's internal list. This method passes ownership
   * of the point to the caller who is expected to delete it when done with it.
   *
   * @return @b ControlPoint* A pointer to the control point. The caller assumes ownership of the
   *                          ControlPoint and is expected to delete it when done. If there are no
   *                          points to return, a NULL pointer is returned.
   */
  ControlPoint *ControlNetVersioner::takeFirstPoint() {
    ControlPoint *point = NULL;
    if ( !m_points.isEmpty() ) {
      point = m_points.takeFirst();
    }

    return point;
  }


  /**
   * Generates a Pvl file from the currently stored control points and header.
   *
   * @return Pvl The Pvl version of the network
   */
  Pvl ControlNetVersioner::toPvl(){
    Pvl pvl;
    pvl.addObject(PvlObject("ControlNetwork"));
    PvlObject &network = pvl.findObject("ControlNetwork");

    network += PvlKeyword("NetworkId", m_header.networkID);
    network += PvlKeyword("TargetName", m_header.targetName);
    network += PvlKeyword("UserName", m_header.userName);
    network += PvlKeyword("Created", m_header.created);
    network += PvlKeyword("LastModified", m_header.lastModified);
    network += PvlKeyword("Description", m_header.description);

    // This is the Pvl version we're converting to
    network += PvlKeyword("Version", "5");

    foreach (ControlPoint *controlPoint, m_points) {
      PvlObject pvlPoint("ControlPoint");

      if ( controlPoint->GetType() == ControlPoint::Fixed ) {
        pvlPoint += PvlKeyword("PointType", "Fixed");
      }

      else if ( controlPoint->GetType() == ControlPoint::Constrained ) {
        pvlPoint += PvlKeyword("PointType", "Constrained");
      }

      else {
        pvlPoint += PvlKeyword("PointType", "Free");
      }

      if ( controlPoint->GetId().isEmpty() ) {
        QString msg = "Unable to write control net to PVL file. "
                      "Invalid control point has no point ID value.";
        throw IException(IException::Unknown, msg, _FILEINFO_);
      }
      else {
        pvlPoint += PvlKeyword("PointId", controlPoint->GetId());
      }
      if ( QString::compare(controlPoint->GetChooserName(), "Null", Qt::CaseInsensitive) != 0 ) {
        pvlPoint += PvlKeyword("ChooserName", controlPoint->GetChooserName());
      }
      if ( QString::compare(controlPoint->GetDateTime(), "Null", Qt::CaseInsensitive) != 0 ) {
        pvlPoint += PvlKeyword("DateTime", controlPoint->GetDateTime());
      }
      if ( controlPoint->IsEditLocked() ) {
        pvlPoint += PvlKeyword("EditLock", "True");
      }
      if ( controlPoint->IsIgnored() ) {
        pvlPoint += PvlKeyword("Ignore", "True");
      }

      switch ( controlPoint->GetAprioriSurfacePointSource() ) {
        case ControlPoint::SurfacePointSource::None:
          break;
        case ControlPoint::SurfacePointSource::User:
          pvlPoint += PvlKeyword("AprioriXYZSource", "User");
          break;
        case ControlPoint::SurfacePointSource::AverageOfMeasures:
          pvlPoint += PvlKeyword("AprioriXYZSource", "AverageOfMeasures");
          break;
        case ControlPoint::SurfacePointSource::Reference:
          pvlPoint += PvlKeyword("AprioriXYZSource", "Reference");
          break;
        case ControlPoint::SurfacePointSource::Basemap:
          pvlPoint += PvlKeyword("AprioriXYZSource", "Basemap");
          break;
        case ControlPoint::SurfacePointSource::BundleSolution:
          pvlPoint += PvlKeyword("AprioriXYZSource", "BundleSolution");
          break;
      }

      if ( controlPoint->HasAprioriSurfacePointSourceFile() ) {
        pvlPoint += PvlKeyword("AprioriXYZSourceFile",
                        controlPoint->GetAprioriSurfacePointSourceFile());
      }

      switch ( controlPoint->GetAprioriRadiusSource() ) {
        case ControlPoint::RadiusSource::None:
          break;
        case ControlPoint::RadiusSource::User:
          pvlPoint += PvlKeyword("AprioriRadiusSource", "User");
          break;
        case ControlPoint::RadiusSource::AverageOfMeasures:
          pvlPoint += PvlKeyword("AprioriRadiusSource", "AverageOfMeasures");
          break;
        case ControlPoint::RadiusSource::BundleSolution:
          pvlPoint += PvlKeyword("AprioriRadiusSource", "BundleSolution");
          break;
        case ControlPoint::RadiusSource::Ellipsoid:
          pvlPoint += PvlKeyword("AprioriRadiusSource", "Ellipsoid");
          break;
        case ControlPoint::RadiusSource::DEM:
          pvlPoint += PvlKeyword("AprioriRadiusSource", "DEM");
          break;
      }

      if ( controlPoint->HasAprioriRadiusSourceFile() ) {
        pvlPoint += PvlKeyword("AprioriRadiusSourceFile",
                        controlPoint->GetAprioriRadiusSourceFile());
        }

      // add surface point x/y/z, convert to lat,lon,radius and output as comment
      SurfacePoint aprioriSurfacePoint = controlPoint->GetAprioriSurfacePoint();
      if ( aprioriSurfacePoint.Valid() ) {
        PvlKeyword aprioriX("AprioriX", toString(aprioriSurfacePoint.GetX().meters()), "meters");
        PvlKeyword aprioriY("AprioriY", toString(aprioriSurfacePoint.GetY().meters()), "meters");
        PvlKeyword aprioriZ("AprioriZ", toString(aprioriSurfacePoint.GetZ().meters()), "meters");

        aprioriX.addComment("AprioriLatitude = "
                            + toString(aprioriSurfacePoint.GetLatitude().degrees())
                            + " <degrees>");
        aprioriY.addComment("AprioriLongitude = "
                            + toString(aprioriSurfacePoint.GetLongitude().degrees())
                            + " <degrees>");

        aprioriZ.addComment("AprioriRadius = "
                            + toString(aprioriSurfacePoint.GetLocalRadius().meters())
                            + " <meters>");

        pvlPoint += aprioriX;
        pvlPoint += aprioriY;
        pvlPoint += aprioriZ;

        symmetric_matrix<double, upper> aprioriCovarianceMatrix =
              aprioriSurfacePoint.GetRectangularMatrix();

        if ( aprioriCovarianceMatrix.size1() > 0 ) {

          // Matrix units are meters squared
          PvlKeyword matrix("AprioriCovarianceMatrix");
          matrix += toString(aprioriCovarianceMatrix(0, 0));
          matrix += toString(aprioriCovarianceMatrix(0, 1));
          matrix += toString(aprioriCovarianceMatrix(0, 2));
          matrix += toString(aprioriCovarianceMatrix(1, 1));
          matrix += toString(aprioriCovarianceMatrix(1, 2));
          matrix += toString(aprioriCovarianceMatrix(2, 2));

          // *** TODO *** What do we do in the case of bundled in rectangular coordinates?
          // For now we do nothing.
          if ( aprioriSurfacePoint.GetLatSigmaDistance().meters() != Isis::Null
               && aprioriSurfacePoint.GetLonSigmaDistance().meters() != Isis::Null
               && aprioriSurfacePoint.GetLocalRadiusSigma().meters() != Isis::Null ) {

            QString sigmas = "AprioriLatitudeSigma = "
              + toString(aprioriSurfacePoint.GetLatSigmaDistance().meters())
              + " <meters>  AprioriLongitudeSigma = "
              + toString(aprioriSurfacePoint.GetLonSigmaDistance().meters())
              + " <meters>  AprioriRadiusSigma = "
              + toString(aprioriSurfacePoint.GetLocalRadiusSigma().meters())
              + " <meters>";
            matrix.addComment(sigmas);
          }

          // If the covariance matrix has a value, add it to the PVL point.
          if ( aprioriCovarianceMatrix(0, 0) != 0.0
               || aprioriCovarianceMatrix(0, 1) != 0.0
               || aprioriCovarianceMatrix(0, 2) != 0.0
               || aprioriCovarianceMatrix(1, 1) != 0.0
               || aprioriCovarianceMatrix(1, 2) != 0.0
               || aprioriCovarianceMatrix(2, 2) != 0.0 ) {

              pvlPoint += matrix;
            }
        }
      }

      // Deal with the generalization here.  *** TODO ***
      // Once we have a coordinate type in the header, we should specify the correct coordinate
      if ( controlPoint->IsCoord1Constrained() ) {
        pvlPoint += PvlKeyword("LatitudeConstrained", "True");
      }

      if ( controlPoint->IsCoord2Constrained() ) {
        pvlPoint += PvlKeyword("LongitudeConstrained", "True");
      }

      if ( controlPoint->IsCoord2Constrained() ) {
        pvlPoint += PvlKeyword("RadiusConstrained", "True");
      }

      // adj surface point, convert to lat,lon,radius and output as comment
      SurfacePoint adjustedSurfacePoint = controlPoint->GetAdjustedSurfacePoint();
      if ( adjustedSurfacePoint.Valid() ) {
        PvlKeyword adjustedX("AdjustedX",
                             toString(adjustedSurfacePoint.GetX().meters()), "meters");
        PvlKeyword adjustedY("AdjustedY",
                             toString(adjustedSurfacePoint.GetY().meters()), "meters");
        PvlKeyword adjustedZ("AdjustedZ",
                             toString(adjustedSurfacePoint.GetZ().meters()), "meters");

        adjustedX.addComment("AdjustedLatitude = "
                             + toString(adjustedSurfacePoint.GetLatitude().degrees())
                             + " <degrees>");
        adjustedY.addComment("AdjustedLongitude = "
                             + toString(adjustedSurfacePoint.GetLongitude().degrees())
                             + " <degrees>");
        adjustedZ.addComment("AdjustedRadius = "
                             + toString(adjustedSurfacePoint.GetLocalRadius().meters())
                             + " <meters>");

        pvlPoint += adjustedX;
        pvlPoint += adjustedY;
        pvlPoint += adjustedZ;

        symmetric_matrix<double, upper> adjustedCovarianceMatrix =
              adjustedSurfacePoint.GetRectangularMatrix();

        if ( adjustedCovarianceMatrix.size1() > 0 ) {

          PvlKeyword matrix("AdjustedCovarianceMatrix");
          matrix += toString(adjustedCovarianceMatrix(0, 0));
          matrix += toString(adjustedCovarianceMatrix(0, 1));
          matrix += toString(adjustedCovarianceMatrix(0, 2));
          matrix += toString(adjustedCovarianceMatrix(1, 1));
          matrix += toString(adjustedCovarianceMatrix(1, 2));
          matrix += toString(adjustedCovarianceMatrix(2, 2));

          if ( adjustedSurfacePoint.GetLatSigmaDistance().meters() != Isis::Null
               && adjustedSurfacePoint.GetLonSigmaDistance().meters() != Isis::Null
               && adjustedSurfacePoint.GetLocalRadiusSigma().meters() != Isis::Null ) {

            QString sigmas = "AdjustedLatitudeSigma = "
              + toString(adjustedSurfacePoint.GetLatSigmaDistance().meters())
              + " <meters>  AdjustedLongitudeSigma = "
              + toString(adjustedSurfacePoint.GetLonSigmaDistance().meters())
              + " <meters>  AdjustedRadiusSigma = "
              + toString(adjustedSurfacePoint.GetLocalRadiusSigma().meters())
              + " <meters>";

            matrix.addComment(sigmas);
          }
          // If the covariance matrix has a value, add it to the PVL point.
          if ( adjustedCovarianceMatrix(0, 0) != 0.0
               || adjustedCovarianceMatrix(0, 1) != 0.0
               || adjustedCovarianceMatrix(0, 2) != 0.0
               || adjustedCovarianceMatrix(1, 1) != 0.0
               || adjustedCovarianceMatrix(1, 2) != 0.0
               || adjustedCovarianceMatrix(2, 2) != 0.0 ) {

            pvlPoint += matrix;
          }
        }
      }

      for (int j = 0; j < controlPoint->GetNumMeasures(); j++) {
        PvlGroup pvlMeasure("ControlMeasure");
        const ControlMeasure &controlMeasure = *controlPoint->GetMeasure(j);
        pvlMeasure += PvlKeyword("SerialNumber", controlMeasure.GetCubeSerialNumber());

        switch ( controlMeasure.GetType() ) {
          case ControlMeasure::Candidate:
            pvlMeasure += PvlKeyword("MeasureType", "Candidate");
            break;
          case ControlMeasure::Manual:
            pvlMeasure += PvlKeyword("MeasureType", "Manual");
            break;
          case ControlMeasure::RegisteredPixel:
            pvlMeasure += PvlKeyword("MeasureType", "RegisteredPixel");
            break;
          case ControlMeasure::RegisteredSubPixel:
            pvlMeasure += PvlKeyword("MeasureType", "RegisteredSubPixel");
            break;
        }

        if (QString::compare(controlMeasure.GetChooserName(), "Null", Qt::CaseInsensitive) != 0) {
          pvlMeasure += PvlKeyword("ChooserName", controlMeasure.GetChooserName());
        }
        if (QString::compare(controlMeasure.GetDateTime(), "Null", Qt::CaseInsensitive) != 0) {
          pvlMeasure += PvlKeyword("DateTime", controlMeasure.GetDateTime());
        }
        if ( controlMeasure.IsEditLocked() ) {
          pvlMeasure += PvlKeyword("EditLock", "True");
        }

        if ( controlMeasure.IsIgnored() ) {
          pvlMeasure += PvlKeyword("Ignore", "True");
        }

        if ( controlMeasure.GetSample() != Isis::Null) {
          pvlMeasure += PvlKeyword("Sample", toString(controlMeasure.GetSample()));

        }

        if ( controlMeasure.GetLine() != Isis::Null ) {
          pvlMeasure += PvlKeyword("Line", toString(controlMeasure.GetLine()));
        }

        if ( controlMeasure.GetDiameter() != Isis::Null
             && controlMeasure.GetDiameter() != 0. ) {
          pvlMeasure += PvlKeyword("Diameter", toString(controlMeasure.GetDiameter()));
        }

        if ( controlMeasure.GetAprioriSample() != Isis::Null ) {
          pvlMeasure += PvlKeyword("AprioriSample", toString(controlMeasure.GetAprioriSample()));
        }

        if ( controlMeasure.GetAprioriLine() != Isis::Null ) {
          pvlMeasure += PvlKeyword("AprioriLine", toString(controlMeasure.GetAprioriLine()));
        }

        if ( controlMeasure.GetSampleSigma() != Isis::Null ) {
          pvlMeasure += PvlKeyword("SampleSigma", toString(controlMeasure.GetSampleSigma()),
                                   "pixels");
        }

        if ( controlMeasure.GetLineSigma() != Isis::Null ) {
          pvlMeasure += PvlKeyword("LineSigma", toString(controlMeasure.GetLineSigma()),
                                   "pixels");
        }

        if ( controlMeasure.GetSampleResidual() != Isis::Null ) {
          pvlMeasure += PvlKeyword("SampleResidual",
                                   toString(controlMeasure.GetSampleResidual()),
                                   "pixels");
        }

        if ( controlMeasure.GetLineResidual() != Isis::Null ) {
          pvlMeasure += PvlKeyword("LineResidual", toString(controlMeasure.GetLineResidual()),
                                   "pixels");
        }

        if ( controlMeasure.IsRejected() ) {
          pvlMeasure += PvlKeyword("JigsawRejected", toString(controlMeasure.IsRejected()));
        }

        foreach (ControlMeasureLogData log, controlMeasure.GetLogDataEntries()) {
          pvlMeasure += log.ToKeyword();
        }

        if ( controlPoint->HasRefMeasure()
             && controlPoint->IndexOfRefMeasure() == j
             && controlPoint->IsReferenceExplicit() ) {
          pvlMeasure += PvlKeyword("Reference", "True");
        }
        pvlPoint.addGroup(pvlMeasure);
      }
      network.addObject(pvlPoint);
    }
    return pvl;
  }


  /**
   * Read a control network file and prepare the data to be converted into
   * a control network.
   *
   * @param netFile The control network file to read.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::read(const FileName netFile, Progress *progress) {
    try {
      // Check if file is a Parquet file by extension
      QString filename = netFile.expanded();
      if (filename.endsWith(".parquet", Qt::CaseInsensitive)) {
        readParquet(netFile, progress);
        return;
      }

      const Pvl &network(netFile.expanded());

      if ( network.hasObject("ProtoBuffer") ) {
        readProtobuf(network, netFile, progress);
      }
      else if ( network.hasObject("ControlNetwork") ) {
        readPvl(network, progress);
      }
      else {
        QString msg = "Could not determine the control network file type";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
    }
    catch (IException &e) {
      QString msg = "Reading the control network [" + netFile.name()
                    + "] failed";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }
  }


  /**
   * Read a Pvl control network and prepare the data to be converted into a
   * control network.
   *
   * @param network The Pvl network data
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readPvl(const Pvl &network, Progress *progress) {
    const PvlObject &controlNetwork = network.findObject("ControlNetwork");

    int version = 1;

    if ( controlNetwork.hasKeyword("Version") ) {
      version = toInt(controlNetwork["Version"][0]);
    }

    switch ( version ) {
      case 1:
        readPvlV0001(controlNetwork, progress);
        break;
      case 2:
        readPvlV0002(controlNetwork, progress);
        break;
      case 3:
        readPvlV0003(controlNetwork, progress);
        break;
      case 4:
        readPvlV0004(controlNetwork, progress);
        break;
      case 5:
        readPvlV0005(controlNetwork, progress);
        break;
      default:
        QString msg = "The Pvl file version [" + toString(version)
                      + "] is not supported";
        throw IException(IException::Unknown, msg, _FILEINFO_);
    }
  }


  /**
   * read a version 1 Pvl control network and convert the data into control points.
   *
   * @param network The control network PvlObject.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readPvlV0001(const PvlObject &network, Progress *progress) {
    // initialize the header
    ControlNetHeaderV0001 header;

    try {
      header.networkID = network.findKeyword("NetworkId")[0];
      header.targetName = network.findKeyword("TargetName")[0];
      header.created = network.findKeyword("Created")[0];
      header.lastModified = network.findKeyword("LastModified")[0];
      header.description = network.findKeyword("Description")[0];
      header.userName = network.findKeyword("UserName")[0];
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(network.objects());
      progress->CheckStatus();
    }

    // initialize the control points
    for (int objectIndex = 0; objectIndex < network.objects(); objectIndex ++) {
      try {

        PvlObject pointObject = network.object(objectIndex);
        ControlPointV0001 point(pointObject, m_header.targetName);

        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }

      }
      catch (IException &e) {
        QString msg = "Failed to initialize control point at index ["
                      + toString(objectIndex) + "].";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * read a version 2 Pvl control network and convert the data into control points.
   *
   * @param network The control network PvlObject.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readPvlV0002(const PvlObject &network, Progress *progress) {
    // initialize the header
    try {
      ControlNetHeaderV0002 header;
      header.networkID = network.findKeyword("NetworkId")[0];
      header.targetName = network.findKeyword("TargetName")[0];
      header.created = network.findKeyword("Created")[0];
      header.lastModified = network.findKeyword("LastModified")[0];
      header.description = network.findKeyword("Description")[0];
      header.userName = network.findKeyword("UserName")[0];
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(network.objects());
      progress->CheckStatus();
    }

    // initialize the control points
    for (int objectIndex = 0; objectIndex < network.objects(); objectIndex ++) {
      try {
        PvlObject pointObject = network.object(objectIndex);
        ControlPointV0002 point(pointObject);

        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }

      }
      catch (IException &e) {
        QString msg = "Failed to initialize control point at index ["
                      + toString(objectIndex) + "].";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * read a version 3 Pvl control network and convert the data into control points.
   *
   * @param network The control network PvlObject.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readPvlV0003(const PvlObject &network, Progress *progress) {
    // initialize the header
    try {
      ControlNetHeaderV0003 header;
      header.networkID = network.findKeyword("NetworkId")[0];
      header.targetName = network.findKeyword("TargetName")[0];
      header.created = network.findKeyword("Created")[0];
      header.lastModified = network.findKeyword("LastModified")[0];
      header.description = network.findKeyword("Description")[0];
      header.userName = network.findKeyword("UserName")[0];
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(network.objects());
      progress->CheckStatus();
    }

    // initialize the control points
    for (int objectIndex = 0; objectIndex < network.objects(); objectIndex ++) {
      try {
        PvlObject pointObject = network.object(objectIndex);
        ControlPointV0003 point(pointObject);
        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }

      }
      catch (IException &e) {
        QString msg = "Failed to initialize control point at index ["
                      + toString(objectIndex) + "].";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * read a version 4 Pvl control network and convert the data into control points.
   *
   * @param network The control network PvlObject.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readPvlV0004(const PvlObject &network, Progress *progress) {
    // initialize the header
    try {
      ControlNetHeaderV0004 header;
      header.networkID = network.findKeyword("NetworkId")[0];
      header.targetName = network.findKeyword("TargetName")[0];
      header.created = network.findKeyword("Created")[0];
      header.lastModified = network.findKeyword("LastModified")[0];
      header.description = network.findKeyword("Description")[0];
      header.userName = network.findKeyword("UserName")[0];
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(network.objects());
      progress->CheckStatus();
    }

    // initialize the control points
    for (int objectIndex = 0; objectIndex < network.objects(); objectIndex ++) {
      try {
        PvlObject pointObject = network.object(objectIndex);
        ControlPointV0004 point(pointObject);
        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }
      }
      catch (IException &e) {
        QString msg = "Failed to initialize control point at index ["
                      + toString(objectIndex) + "].";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * read a version 5 Pvl control network and convert the data into control points.
   *
   * @param network The control network PvlObject.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readPvlV0005(const PvlObject &network, Progress *progress) {
    // initialize the header
    try {
      ControlNetHeaderV0005 header;
      header.networkID = network.findKeyword("NetworkId")[0];
      header.targetName = network.findKeyword("TargetName")[0];
      header.created = network.findKeyword("Created")[0];
      header.lastModified = network.findKeyword("LastModified")[0];
      header.description = network.findKeyword("Description")[0];
      header.userName = network.findKeyword("UserName")[0];
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(network.objects());
      progress->CheckStatus();
    }

    // initialize the control points
    for (int objectIndex = 0; objectIndex < network.objects(); objectIndex ++) {
      try {
        PvlObject pointObject = network.object(objectIndex);
        ControlPointV0005 point(pointObject);
        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }
      }
      catch (IException &e) {
        QString msg = "Failed to initialize control point at index ["
                      + toString(objectIndex) + "].";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * Read a protobuf control network and prepare the data to be converted into a
   * control network.
   *
   * @param header The Pvl network header that contains the version number.
   * @param netFile The filename of the control network file.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readProtobuf(const Pvl &header,
                                         const FileName netFile,
                                         Progress *progress) {
    int version = 1;

    const PvlObject &protoBuf = header.findObject("ProtoBuffer");
    const PvlGroup &netInfo = protoBuf.findGroup("ControlNetworkInfo");

    if ( netInfo.hasKeyword("Version") ) {
      version = toInt(netInfo["Version"][0]);
    }
    switch ( version ) {
      case 1:
        readProtobufV0001(header, netFile, progress);
        break;
      case 2:
        readProtobufV0002(header, netFile, progress);
        break;
      case 5:
        readProtobufV0005(header, netFile, progress);
        break;
      default:
        QString msg = "The Protobuf file version [" + toString(version)
                      + "] is not supported";
        throw IException(IException::Io, msg, _FILEINFO_);
    }
  }


  /**
   * Read a protobuf version 1 control network and prepare the data to be
   * converted into a control network.
   *
   * @param header The Pvl file header that contains byte offsets for the protobuf messages
   * @param netFile The filename of the control network file.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readProtobufV0001(const Pvl &header,
                                              const FileName netFile,
                                              Progress *progress) {
    const PvlObject &protoBufferInfo = header.findObject("ProtoBuffer");
    const PvlObject &protoBufferCore = protoBufferInfo.findObject("Core");

    BigInt coreStartPos = protoBufferCore["StartByte"];
    BigInt coreLength = protoBufferCore["Bytes"];

    fstream input(netFile.expanded().toLatin1().data(), ios::in | ios::binary);
    if ( !input.is_open() ) {
      QString msg = "Failed to open protobuf file [" + netFile.name() + "].";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    input.seekg(coreStartPos, ios::beg);
    IstreamInputStream inStream(&input);
    CodedInputStream codedInStream(&inStream);
    codedInStream.PushLimit(coreLength);
    // max 512MB, warn at 400MB
    codedInStream.SetTotalBytesLimit(1024 * 1024 * 512);

    // Now stream the rest of the input into the google protocol buffer.
    ControlNetFileProtoV0001 protoNet;
    try {
      if ( !protoNet.ParseFromCodedStream(&codedInStream) ) {
        QString msg = "Failed to read input PB file [" + netFile.name() + "].";
        throw IException(IException::Programmer, msg, _FILEINFO_);
      }
    }
    catch (IException &e) {
      QString msg = "Cannot parse binary protobuf file";
      throw IException(e, IException::User, msg, _FILEINFO_);
    }
    catch (...) {
      QString msg = "Cannot parse binary PB file";
      throw IException(IException::User, msg, _FILEINFO_);
    }

    const PvlObject &logDataInfo = protoBufferInfo.findObject("LogData");
    BigInt logStartPos = logDataInfo["StartByte"];
    BigInt logLength = logDataInfo["Bytes"];

    input.clear();
    input.seekg(logStartPos, ios::beg);
    IstreamInputStream logInStream(&input);
    CodedInputStream codedLogInStream(&logInStream);
    codedLogInStream.PushLimit(logLength);
    // max 512MB, warn at 400MB
    codedLogInStream.SetTotalBytesLimit(1024 * 1024 * 512);

    // Now stream the rest of the input into the google protocol buffer.
    ControlNetLogDataProtoV0001 protoLogData;
    try {
      if ( !protoLogData.ParseFromCodedStream(&codedLogInStream) ) {
        QString msg = "Failed to read log data in protobuf file [" + netFile.name() + "].";
        throw IException(IException::Programmer, msg, _FILEINFO_);
      }
    }
    catch (...) {
      QString msg = "Cannot parse binary protobuf file's log data";
      throw IException(IException::User, msg, _FILEINFO_);
    }

    // Create the header
    try {
      ControlNetHeaderV0002 header;
      header.networkID = protoNet.networkid().c_str();
      if ( protoNet.has_targetname() ) {
        header.targetName = protoNet.targetname().c_str();
      }
      else {
        header.targetName = "";
      }
      header.created = protoNet.created().c_str();
      header.lastModified = protoNet.lastmodified().c_str();
      header.description = protoNet.description().c_str();
      header.userName = protoNet.username().c_str();
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Failed to parse the header from the protobuf control network file.";
      throw IException(e, IException::User, msg, _FILEINFO_);
    }

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps( protoNet.points_size() );
      progress->CheckStatus();
    }

    // Create the control points
    for (int i = 0; i < protoNet.points_size(); i++) {
      try {
        QSharedPointer<ControlNetFileProtoV0001_PBControlPoint>
              protoPoint(new ControlNetFileProtoV0001_PBControlPoint(protoNet.points(i)));
        QSharedPointer<ControlNetLogDataProtoV0001_Point>
              protoPointLogData(new ControlNetLogDataProtoV0001_Point(protoLogData.points(i)));
        ControlPointV0002 point(protoPoint, protoPointLogData);
        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }

      }
      catch (IException &e) {
        QString msg = "Failed to convert version 1 protobuf control point at index ["
                      + toString(i) + "] into a ControlPoint.";
        throw IException(e, IException::User, msg, _FILEINFO_);
      }
    }
  }


  /**
   * Read a protobuf version 2 control network and prepare the data to be
   *  converted into a control network.
   *
   * @param header The Pvl file header that contains byte offsets for the protobuf messages
   * @param netFile The filename of the control network file.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readProtobufV0002(const Pvl &header,
                                              const FileName netFile,
                                              Progress *progress) {
    // read the header protobuf object
    const PvlObject &protoBufferInfo = header.findObject("ProtoBuffer");
    const PvlObject &protoBufferCore = protoBufferInfo.findObject("Core");

    BigInt headerStartPos = protoBufferCore["HeaderStartByte"];
    BigInt headerLength = protoBufferCore["HeaderBytes"];

    fstream input(netFile.expanded().toLatin1().data(), ios::in | ios::binary);
    if ( !input.is_open() ) {
      QString msg = "Failed to open control network file" + netFile.name();
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    input.seekg(headerStartPos, ios::beg);
    streampos filePos = input.tellg();

    ControlNetFileHeaderV0002 protoHeader;
    try {
      IstreamInputStream headerInStream(&input);
      CodedInputStream headerCodedInStream(&headerInStream);
      // max 512MB, warn at 400MB
      headerCodedInStream.SetTotalBytesLimit(1024 * 1024 * 512);
      CodedInputStream::Limit oldLimit = headerCodedInStream.PushLimit(headerLength);
      if ( !protoHeader.ParseFromCodedStream(&headerCodedInStream) ) {
        QString msg = "Failed to parse protobuf header from input control net file ["
                      + netFile.name() + "]";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
      headerCodedInStream.PopLimit(oldLimit);
      filePos += headerLength;
    }
    catch (...) {
      QString msg = "An error occured while reading the protobuf control network header.";
      throw IException(IException::Io, msg, _FILEINFO_);
    }

    // initialize the header from the protobuf header
    try {
      ControlNetHeaderV0004 header;
      header.networkID = protoHeader.networkid().c_str();
      if ( protoHeader.has_targetname() ) {
        header.targetName = protoHeader.targetname().c_str();
      }
      else {
        header.targetName = "";
      }
      header.created = protoHeader.created().c_str();
      header.lastModified = protoHeader.lastmodified().c_str();
      header.description = protoHeader.description().c_str();
      header.userName = protoHeader.username().c_str();
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    // read each protobuf control point and then initialize it
    // For some reason, reading the header causes the input stream to fail so reopen the file
    input.close();
    input.open(netFile.expanded().toLatin1().data(), ios::in | ios::binary);
    input.seekg(filePos, ios::beg);
    IstreamInputStream pointInStream(&input);
    int numPoints = protoHeader.pointmessagesizes_size();

    if (progress) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(numPoints);
      progress->CheckStatus();
    }

    for (int pointIndex = 0; pointIndex < numPoints; pointIndex ++) {
      QSharedPointer<ControlPointFileEntryV0002> newPoint(new ControlPointFileEntryV0002);

      try {
        CodedInputStream pointCodedInStream(&pointInStream);
        pointCodedInStream.SetTotalBytesLimit(1024 * 1024 * 512);
        int pointSize = protoHeader.pointmessagesizes(pointIndex);
        CodedInputStream::Limit oldPointLimit = pointCodedInStream.PushLimit(pointSize);
        newPoint->ParseFromCodedStream(&pointCodedInStream);
        pointCodedInStream.PopLimit(oldPointLimit);
      }
      catch (...) {
        QString msg = "Failed to read protobuf version 2 control point at index ["
                      + toString(pointIndex) + "].";
        throw IException(IException::Io, msg, _FILEINFO_);
      }

      try {
        ControlPointV0004 point(newPoint);
        m_points.append( createPoint(point) );

        if (progress) {
          progress->CheckStatus();
        }

      }
      catch (IException &e) {
        QString msg = "Failed to convert protobuf version 2 control point at index ["
                      + toString(pointIndex) + "] into a ControlPoint.";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * Read a protobuf version 5 control network and prepare the data to be
   *  converted into a control network.
   *
   * @param header The Pvl file header that contains byte offsets for the protobuf messages
   * @param netFile The filename of the control network file.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readProtobufV0005(const Pvl &header,
                                              const FileName netFile,
                                              Progress *progress) {

    // read the header protobuf object
    const PvlObject &protoBufferInfo = header.findObject("ProtoBuffer");
    const PvlObject &protoBufferCore = protoBufferInfo.findObject("Core");

    BigInt headerStartPos = protoBufferCore["HeaderStartByte"];
    BigInt headerLength = protoBufferCore["HeaderBytes"];
    BigInt pointsLength = protoBufferCore["PointsBytes"];

    fstream input(netFile.expanded().toLatin1().data(), ios::in | ios::binary);
    if ( !input.is_open() ) {
      QString msg = "Failed to open control network file" + netFile.name();
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    input.seekg(headerStartPos, ios::beg);

    streampos filePos = input.tellg();

    ControlNetFileHeaderV0005 protoHeader;
    try {

      IstreamInputStream headerInStream(&input);
      CodedInputStream headerCodedInStream(&headerInStream);

      // max 512MB, warn at 400MB
      headerCodedInStream.SetTotalBytesLimit(1024 * 1024 * 512);

      CodedInputStream::Limit oldLimit = headerCodedInStream.PushLimit(headerLength);

      if ( !protoHeader.ParseFromCodedStream(&headerCodedInStream) ) {
        QString msg = "Failed to parse protobuf header from input control net file ["
                      + netFile.name() + "]";
        throw IException(IException::Io, msg, _FILEINFO_);
      }

      headerCodedInStream.PopLimit(oldLimit);

      filePos += headerLength;
    }
    catch (...) {
      QString msg = "An error occured while reading the protobuf control network header.";
      throw IException(IException::Io, msg, _FILEINFO_);
    }
    // initialize the header from the protobuf header
    try {
      ControlNetHeaderV0005 header;
      header.networkID = protoHeader.networkid().c_str();
      if ( protoHeader.has_targetname() ) {
        header.targetName = protoHeader.targetname().c_str();
      }
      else {
        header.targetName = "";
      }
      header.created = protoHeader.created().c_str();
      header.lastModified = protoHeader.lastmodified().c_str();
      header.description = protoHeader.description().c_str();
      header.userName = protoHeader.username().c_str();
      createHeader(header);
    }
    catch (IException &e) {
      QString msg = "Missing required header information.";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }

    // read each protobuf control point and then initialize it
    // For some reason, reading the header causes the input stream to fail so reopen the file
    input.close();
    input.open(netFile.expanded().toLatin1().data(), ios::in | ios::binary);
    input.seekg(filePos, ios::beg);

    IstreamInputStream pointInStream(&input);

    BigInt numberOfPoints = 0;

    if ( protoBufferInfo.hasGroup("ControlNetworkInfo") ) {
      const PvlGroup &networkInfo = protoBufferInfo.findGroup("ControlNetworkInfo");

      if ( networkInfo.hasKeyword("NumberOfPoints") ) {
        try {
          numberOfPoints = networkInfo["NumberOfPoints"];
        }
        catch (...) {
          numberOfPoints = 0;
        }
      }
    }

    if (progress && numberOfPoints != 0) {
      progress->SetText("Reading Control Points...");
      progress->SetMaximumSteps(numberOfPoints);
      progress->CheckStatus();
    }

    Isis::EndianSwapper lsb("LSB");
    int pointIndex = -1;
    while (pointInStream.ByteCount() < pointsLength) {
      pointIndex += 1;
      QSharedPointer<ControlPointFileEntryV0002> newPoint(new ControlPointFileEntryV0002);

      try {

        CodedInputStream pointCodedInStream(&pointInStream);
        pointCodedInStream.SetTotalBytesLimit(1024 * 1024 * 512);

        uint32_t size;
        pointCodedInStream.ReadRaw(reinterpret_cast<char *>(&size), sizeof(size));

        size = lsb.Uint32_t(&size);

        CodedInputStream::Limit oldPointLimit = pointCodedInStream.PushLimit(size);
        newPoint->ParseFromCodedStream(&pointCodedInStream);
        pointCodedInStream.PopLimit(oldPointLimit);
      }
      catch (...) {
        QString msg = "Failed to read protobuf version 2 control point at index ["
                      + toString(pointIndex) + "].";
        throw IException(IException::Io, msg, _FILEINFO_);
      }

      try {
        ControlPointV0005 point(newPoint);
        m_points.append( createPoint(point) );

        if (progress && numberOfPoints != 0) {
          progress->CheckStatus();
        }

      }
      catch (IException &e) {
        QString msg = "Failed to convert protobuf version 2 control point at index ["
                      + toString(pointIndex) + "] into a ControlPoint.";
        throw IException(e, IException::Io, msg, _FILEINFO_);
      }
    }
  }


  /**
   * Create a pointer to a latest version ControlPoint from an
   * object in a V0001 control net file. This method converts a
   * ControlPointV0001 to the latest ControlPontV#### version
   * and uses the latest versioned point to construct and fill an
   * Isis::ControlPoint.
   *
   * @param point The versioned control point to be updated.
   *
   * @return @b ControlPoint* The ControlPoint constructed from the given point.
   */
  ControlPoint *ControlNetVersioner::createPoint(ControlPointV0001 &point) {
    ControlPointV0002 newPoint(point);
    return createPoint(newPoint);
  }


  /**
   * Create a pointer to a latest version ControlPoint from an
   * object in a V0002 control net file. This method converts a
   * ControlPointV0002 to the latest ControlPontV#### version
   * and uses the latest versioned point to construct and fill an
   * Isis::ControlPoint.
   *
   * @param point The versioned control point to be updated.
   *
   * @return @b ControlPoint* The ControlPoint constructed from the given point.
   */
  ControlPoint *ControlNetVersioner::createPoint(ControlPointV0002 &point) {

    ControlPointV0003 newPoint(point);
    return createPoint(newPoint);

  }


  /**
   * Create a pointer to a latest version ControlPoint from an
   * object in a V0003 control net file. This method converts a
   * ControlPointV0003 to the latest ControlPontV#### version
   * and uses the latest versioned point to construct and fill an
   * Isis::ControlPoint.
   *
   * @param point The versioned control point to be updated.
   *
   * @return @b ControlPoint* The ControlPoint constructed from the given point.
   */
  ControlPoint *ControlNetVersioner::createPoint(ControlPointV0003 &point) {

    ControlPointFileEntryV0002 protoPoint = point.pointData();
    ControlPoint *controlPoint = new ControlPoint;

    // ID is required, no need for if-statement here
    controlPoint->SetId(QString(protoPoint.id().c_str()));

    if ( protoPoint.has_choosername() ) {
      controlPoint->SetChooserName(protoPoint.choosername().c_str());
    }

    // point type is required, no need for if statement here
    ControlPoint::PointType pointType;
    switch ( protoPoint.type() ) {
      case ControlPointFileEntryV0002_PointType_obsolete_Tie:
      case ControlPointFileEntryV0002_PointType_Free:
        pointType = ControlPoint::Free;
        break;
      case ControlPointFileEntryV0002_PointType_Constrained:
        pointType = ControlPoint::Constrained;
        break;
      case ControlPointFileEntryV0002_PointType_obsolete_Ground:
      case ControlPointFileEntryV0002_PointType_Fixed:
        pointType = ControlPoint::Fixed;
        break;
      default:
        QString msg = "Unable to create ControlPoint [" + toString(protoPoint.id().c_str())
                      + "] from file. Type enumeration [" + toString((int)(protoPoint.type()))
                      + "] is invalid.";
        throw IException(IException::Programmer, msg, _FILEINFO_);
        break;
    }
    controlPoint->SetType(pointType);

    if ( protoPoint.has_ignore() ) {
      controlPoint->SetIgnored(protoPoint.ignore());
    }
    if ( protoPoint.has_jigsawrejected() ) {
      controlPoint->SetRejected(protoPoint.jigsawrejected());
    }

    // setting apriori radius information
    if ( protoPoint.has_aprioriradiussource() ) {

      switch ( protoPoint.aprioriradiussource() ) {
        case ControlPointFileEntryV0002_AprioriSource_None:
          controlPoint->SetAprioriRadiusSource(ControlPoint::RadiusSource::None);
          break;
        case ControlPointFileEntryV0002_AprioriSource_User:
          controlPoint->SetAprioriRadiusSource(ControlPoint::RadiusSource::User);
          break;
        case ControlPointFileEntryV0002_AprioriSource_AverageOfMeasures:
          controlPoint->SetAprioriRadiusSource(ControlPoint::RadiusSource::AverageOfMeasures);
          break;
        case ControlPointFileEntryV0002_AprioriSource_Ellipsoid:
          controlPoint->SetAprioriRadiusSource(ControlPoint::RadiusSource::Ellipsoid);
          break;
        case ControlPointFileEntryV0002_AprioriSource_DEM:
          controlPoint->SetAprioriRadiusSource(ControlPoint::RadiusSource::DEM);
          break;
        case ControlPointFileEntryV0002_AprioriSource_BundleSolution:
          controlPoint->SetAprioriRadiusSource(ControlPoint::RadiusSource::BundleSolution);
          break;

        default:
          QString msg = "Unknown control point apriori radius source.";
          throw IException(IException::User, msg, _FILEINFO_);
          break;
      }
    }

    if ( protoPoint.has_aprioriradiussourcefile() ) {
      controlPoint->SetAprioriRadiusSourceFile(protoPoint.aprioriradiussourcefile().c_str());
    }

    // setting apriori surf pt information
    if ( protoPoint.has_apriorisurfpointsource() ) {
      switch ( protoPoint.apriorisurfpointsource() ) {
        case ControlPointFileEntryV0002_AprioriSource_None:
          controlPoint->SetAprioriSurfacePointSource(ControlPoint::SurfacePointSource::None);
         break;

        case ControlPointFileEntryV0002_AprioriSource_User:
          controlPoint->SetAprioriSurfacePointSource(ControlPoint::SurfacePointSource::User);
          break;

        case ControlPointFileEntryV0002_AprioriSource_AverageOfMeasures:
          controlPoint->SetAprioriSurfacePointSource(
                              ControlPoint::SurfacePointSource::AverageOfMeasures);
          break;

        case ControlPointFileEntryV0002_AprioriSource_Reference:
          controlPoint->SetAprioriSurfacePointSource(ControlPoint::SurfacePointSource::Reference);
          break;

        case ControlPointFileEntryV0002_AprioriSource_Basemap:
          controlPoint->SetAprioriSurfacePointSource(ControlPoint::SurfacePointSource::Basemap);
          break;

        case ControlPointFileEntryV0002_AprioriSource_BundleSolution:
          controlPoint->SetAprioriSurfacePointSource(
                              ControlPoint::SurfacePointSource::BundleSolution);
          break;

        default:
          QString msg = "Unknown control point aprioir surface point source.";
          throw IException(IException::User, msg, _FILEINFO_);
          break;
      }
    }

    if ( protoPoint.has_apriorisurfpointsourcefile() ) {
      controlPoint->SetAprioriSurfacePointSourceFile(
                          protoPoint.apriorisurfpointsourcefile().c_str());
    }

    if ( protoPoint.has_apriorix()
        && protoPoint.has_aprioriy()
        && protoPoint.has_aprioriz() ) {

      SurfacePoint aprioriSurfacePoint(Displacement(protoPoint.apriorix(), Displacement::Meters),
                                       Displacement(protoPoint.aprioriy(), Displacement::Meters),
                                       Displacement(protoPoint.aprioriz(), Displacement::Meters));

      if ( protoPoint.aprioricovar_size() > 0 ) {
        symmetric_matrix<double, upper> aprioriCovarianceMatrix;
        aprioriCovarianceMatrix.resize(3);
        aprioriCovarianceMatrix.clear();
        aprioriCovarianceMatrix(0, 0) = protoPoint.aprioricovar(0);
        aprioriCovarianceMatrix(0, 1) = protoPoint.aprioricovar(1);
        aprioriCovarianceMatrix(0, 2) = protoPoint.aprioricovar(2);
        aprioriCovarianceMatrix(1, 1) = protoPoint.aprioricovar(3);
        aprioriCovarianceMatrix(1, 2) = protoPoint.aprioricovar(4);
        aprioriCovarianceMatrix(2, 2) = protoPoint.aprioricovar(5);
        aprioriSurfacePoint.SetRectangularMatrix(aprioriCovarianceMatrix);
        // note: setting lat/lon/rad constrained happens when we call SetAprioriSurfacePoint()
      }

      controlPoint->SetAprioriSurfacePoint(aprioriSurfacePoint);
    }

    // setting adj surf pt information
    if ( protoPoint.has_adjustedx()
        && protoPoint.has_adjustedy()
        && protoPoint.has_adjustedz() ) {

      SurfacePoint adjustedSurfacePoint(Displacement(protoPoint.adjustedx(),Displacement::Meters),
                                        Displacement(protoPoint.adjustedy(),Displacement::Meters),
                                        Displacement(protoPoint.adjustedz(),Displacement::Meters));

      if ( protoPoint.adjustedcovar_size() > 0 ) {
        symmetric_matrix<double, upper> adjustedCovarianceMatrix;
        adjustedCovarianceMatrix.resize(3);
        adjustedCovarianceMatrix.clear();
        adjustedCovarianceMatrix(0, 0) = protoPoint.adjustedcovar(0);
        adjustedCovarianceMatrix(0, 1) = protoPoint.adjustedcovar(1);
        adjustedCovarianceMatrix(0, 2) = protoPoint.adjustedcovar(2);
        adjustedCovarianceMatrix(1, 1) = protoPoint.adjustedcovar(3);
        adjustedCovarianceMatrix(1, 2) = protoPoint.adjustedcovar(4);
        adjustedCovarianceMatrix(2, 2) = protoPoint.adjustedcovar(5);
        adjustedSurfacePoint.SetRectangularMatrix(adjustedCovarianceMatrix);
      }

      controlPoint->SetAdjustedSurfacePoint(adjustedSurfacePoint);
    }

    SurfacePoint aprioriSurfacePoint = controlPoint->GetAprioriSurfacePoint();
    SurfacePoint adjustedSurfacePoint = controlPoint->GetAdjustedSurfacePoint();
    controlPoint->SetAdjustedSurfacePoint(adjustedSurfacePoint);
    controlPoint->SetAprioriSurfacePoint(aprioriSurfacePoint);

    // adding measure information
    for (int m = 0 ; m < protoPoint.measures_size(); m++) {
      controlPoint->Add( createMeasure( protoPoint.measures(m) ) );
    }

    if ( protoPoint.has_referenceindex() ) {
      controlPoint->SetRefMeasure(protoPoint.referenceindex());
    }

    // Set DateTime after calling all setters that clear DateTime value
    if ( protoPoint.has_datetime() ) {
      controlPoint->SetDateTime(protoPoint.datetime().c_str());
    }
    // Set edit lock last
    if ( protoPoint.has_editlock() ) {
      controlPoint->SetEditLock(protoPoint.editlock());
    }

    return controlPoint;

  }


  /**
   * Create a pointer to a ControlMeasure from a V0006 file.
   *
   * @param measure The versioned control measure to be created.
   *
   * @return The ControlMeasure constructed from the V0006 version
   *         file.
   */
  ControlMeasure *ControlNetVersioner::createMeasure(
                                             const ControlPointFileEntryV0002_Measure &measure) {

    ControlMeasure *newMeasure = new ControlMeasure;

    // serial number is required, no need for if-statement
    newMeasure->SetCubeSerialNumber(QString(measure.serialnumber().c_str()));

    // measure type is required, no need for if-statement
    ControlMeasure::MeasureType measureType;
    switch ( measure.type() ) {
      case ControlPointFileEntryV0002_Measure::Candidate:
        measureType = ControlMeasure::Candidate;
        break;
      case ControlPointFileEntryV0002_Measure::Manual:
        measureType = ControlMeasure::Manual;
        break;
      case ControlPointFileEntryV0002_Measure::RegisteredPixel:
        measureType = ControlMeasure::RegisteredPixel;
        break;
      case ControlPointFileEntryV0002_Measure::RegisteredSubPixel:
        measureType = ControlMeasure::RegisteredSubPixel;
        break;
      default:
        QString msg = "Unknown control measure type.";
        throw IException(IException::User, msg, _FILEINFO_);
        break;
    }
    newMeasure->SetType(measureType);

    if ( measure.has_jigsawrejected() ) {
      newMeasure->SetRejected(measure.jigsawrejected());
    }
    if ( measure.has_ignore() ) {
      newMeasure->SetIgnored(measure.ignore());
    }
    if ( measure.has_line() ) {
      newMeasure->SetCoordinate(measure.sample(), measure.line());
    }
    if ( measure.has_diameter() ) {
      newMeasure->SetDiameter(measure.diameter());
    }
    if ( measure.has_apriorisample() ) {
      newMeasure->SetAprioriSample(measure.apriorisample());
    }
    if ( measure.has_aprioriline() ) {
      newMeasure->SetAprioriLine(measure.aprioriline());
    }
    if ( measure.has_samplesigma() ) {
      newMeasure->SetSampleSigma(measure.samplesigma());
    }
    if ( measure.has_linesigma() ) {
      newMeasure->SetLineSigma(measure.linesigma());
    }
    if ( measure.has_sampleresidual()
         && measure.has_lineresidual() ) {
      newMeasure->SetResidual(measure.sampleresidual(), measure.lineresidual());
    }

    for (int i = 0; i < measure.log_size(); i++) {

      const ControlPointFileEntryV0002_Measure_MeasureLogData &protoLog = measure.log(i);
      ControlMeasureLogData logEntry;
      if ( protoLog.has_doubledatatype() ) {
        logEntry.SetDataType((ControlMeasureLogData::NumericLogDataType)protoLog.doubledatatype());
      }
      if ( protoLog.has_doubledatavalue() ) {
        logEntry.SetNumericalValue( protoLog.doubledatavalue() );
      }
      newMeasure->SetLogData(logEntry);
    }

    if ( measure.has_choosername() ) {
      newMeasure->SetChooserName(QString(measure.choosername().c_str()));
    }

    if ( measure.has_datetime() ) {
      newMeasure->SetDateTime(QString(measure.datetime().c_str()));
    }

    // It is VERY important that the edit lock flag is set last because it prevents
    // all of the other mutators from working if true.
    if ( measure.has_editlock() ) {
      newMeasure->SetEditLock(measure.editlock());
    }
    return newMeasure;
  }


  /**
   * Create the internal header from a V0001 header.
   *
   * The latest version is V0001, so this will check for an old issue with
   * Mars target names and then internalize the header.
   *
   * @param header The V0001 header
   */
  void ControlNetVersioner::createHeader(const ControlNetHeaderV0001 header) {
    m_header = header;

    if ( m_header.targetName.startsWith("MRO/") ) {
      m_header.targetName = "Mars";
    }
  }


  /**
   * This will write a control net file object to disk.
   *
   * @param netFile The output filename that will be written to
   *
   */
  void ControlNetVersioner::write(FileName netFile) {
    // Check if file is a Parquet file by extension
    QString filename = netFile.expanded();
    if (filename.endsWith(".parquet", Qt::CaseInsensitive)) {
      writeParquet(netFile);
      return;
    }
    
    try {

      const int labelBytes = 65536;
      fstream output(netFile.expanded().toLatin1().data(), ios::out | ios::trunc | ios::binary);
      char *blankLabel = new char[labelBytes];
      memset(blankLabel, 0, labelBytes);
      output.write(blankLabel, labelBytes);
      delete [] blankLabel;

      int numMeasures = 0;
      int numPoints = m_points.size();
      foreach (ControlPoint *point, m_points) {
        numMeasures += point->GetNumMeasures();
      }

      streampos startCoreHeaderPos = output.tellp();

      writeHeader(&output);

      BigInt pointByteTotal = 0;
      while ( !m_points.isEmpty() ) {
         pointByteTotal += writeFirstPoint(&output);
      }

      // Insert header at the beginning of the file once writing is done.
      ControlNetFileHeaderV0005 protobufHeader;

      protobufHeader.set_networkid(m_header.networkID.toLatin1().data());
      protobufHeader.set_targetname(m_header.targetName.toLatin1().data());
      protobufHeader.set_created(m_header.created.toLatin1().data());
      protobufHeader.set_lastmodified(m_header.lastModified.toLatin1().data());
      protobufHeader.set_description(m_header.description.toLatin1().data());
      protobufHeader.set_username(m_header.userName.toLatin1().data());

      streampos coreHeaderSize = protobufHeader.ByteSizeLong();

      Pvl p;

      PvlObject protoObj("ProtoBuffer");

      PvlObject protoCore("Core");
      protoCore.addKeyword(PvlKeyword("HeaderStartByte",
                           toString((BigInt) startCoreHeaderPos)));
      protoCore.addKeyword(PvlKeyword("HeaderBytes", toString((BigInt) coreHeaderSize)));

      BigInt pointsStartByte = (BigInt) (startCoreHeaderPos + coreHeaderSize);

      protoCore.addKeyword(PvlKeyword("PointsStartByte", toString(pointsStartByte)));

      protoCore.addKeyword(PvlKeyword("PointsBytes",
                           toString(pointByteTotal)));
      protoObj.addObject(protoCore);

      PvlGroup netInfo("ControlNetworkInfo");
      netInfo.addComment("This group is for informational purposes only");
      netInfo += PvlKeyword("NetworkId", protobufHeader.networkid().c_str());
      netInfo += PvlKeyword("TargetName", protobufHeader.targetname().c_str());
      netInfo += PvlKeyword("UserName", protobufHeader.username().c_str());
      netInfo += PvlKeyword("Created", protobufHeader.created().c_str());
      netInfo += PvlKeyword("LastModified", protobufHeader.lastmodified().c_str());
      netInfo += PvlKeyword("Description", protobufHeader.description().c_str());
      netInfo += PvlKeyword("NumberOfPoints", toString(numPoints));
      netInfo += PvlKeyword("NumberOfMeasures", toString(numMeasures));
      netInfo += PvlKeyword("Version", "5");
      protoObj.addGroup(netInfo);

      p.addObject(protoObj);

      output.seekp(0, ios::beg);
      output << p;
      output << '\n';
      output.close();

    }
    catch (...) {
      QString msg = "Can't write control net file";
      throw IException(IException::Io, msg, _FILEINFO_);
    }
  }

 /**
  * This will read the binary protobuffer control network header to an fstream
  *
  * @param output The fstream we're writing out to.
  */
  void ControlNetVersioner::writeHeader(fstream *output) {

    // Create the protobuf header using our struct
    ControlNetFileHeaderV0005 protobufHeader;

    protobufHeader.set_networkid(m_header.networkID.toLatin1().data());
    protobufHeader.set_targetname(m_header.targetName.toLatin1().data());
    protobufHeader.set_created(m_header.created.toLatin1().data());
    protobufHeader.set_lastmodified(m_header.lastModified.toLatin1().data());
    protobufHeader.set_description(m_header.description.toLatin1().data());
    protobufHeader.set_username(m_header.userName.toLatin1().data());

    // Write out the header
    if ( !protobufHeader.SerializeToOstream(output) ) {
      QString msg = "Failed to write output control network file.";
      throw IException(IException::Io, msg, _FILEINFO_);
    }
  }


 /**
  * This will write the first control point to a file stream.
  * The written point will be removed from the versioner and then deleted if the versioner
  * has ownership of it.
  *
  * @param output A pointer to the fileStream that we are writing the point to.
  *
  * @return @b int The number of bytes written to the filestream.
  */
  int ControlNetVersioner::writeFirstPoint(fstream *output) {

      BigInt startPos = output->tellp();

      ControlPointFileEntryV0002 protoPoint;
      ControlPoint *controlPoint = m_points.takeFirst();

      if ( controlPoint->GetId().isEmpty() ) {
        QString msg = "Unbable to write first point of control net. "
                      "Invalid control point has no point ID value.";
        throw IException(IException::Unknown, msg, _FILEINFO_);
      }
      else {
        protoPoint.set_id(controlPoint->GetId().toLatin1().data());
      }

      if ( QString::compare(controlPoint->GetChooserName(), "Null", Qt::CaseInsensitive) != 0 ) {
        protoPoint.set_choosername(controlPoint->GetChooserName().toLatin1().data());
      }
      if ( QString::compare(controlPoint->GetDateTime(), "Null", Qt::CaseInsensitive) != 0 ) {
        protoPoint.set_datetime(controlPoint->GetDateTime().toLatin1().data());
      }
      if ( controlPoint->IsEditLocked() ) {
        protoPoint.set_editlock(true);
      }

      ControlPointFileEntryV0002_PointType pointType;
      switch ( controlPoint->GetType() ) {
        case ControlPoint::Free:
          pointType = ControlPointFileEntryV0002_PointType_Free;
          break;
        case ControlPoint::Constrained:
          pointType = ControlPointFileEntryV0002_PointType_Constrained;
          break;
        case ControlPoint::Fixed:
          pointType = ControlPointFileEntryV0002_PointType_Fixed;
          break;
        default:
          pointType = ControlPointFileEntryV0002_PointType_Free;
          break;
      }
      protoPoint.set_type(pointType);

      if ( controlPoint->IsIgnored() ) {
        protoPoint.set_ignore(true);
      }

      if ( controlPoint->IsRejected() ) {
        protoPoint.set_jigsawrejected(true);
      }

      if ( controlPoint->HasRefMeasure() && controlPoint->IsReferenceExplicit() ) {
        protoPoint.set_referenceindex(controlPoint->IndexOfRefMeasure());
      }

      if ( controlPoint->HasAprioriSurfacePointSourceFile() ) {
        protoPoint.set_apriorisurfpointsourcefile(
                   controlPoint->GetAprioriSurfacePointSourceFile().toLatin1().data());
      }

      // Apriori Surf Point Source ENUM settting
      switch ( controlPoint->GetAprioriSurfacePointSource() ) {
        case ControlPoint::SurfacePointSource::None:
          protoPoint.set_apriorisurfpointsource(ControlPointFileEntryV0002_AprioriSource_None);
          break;
        case ControlPoint::SurfacePointSource::User:
          protoPoint.set_apriorisurfpointsource(ControlPointFileEntryV0002_AprioriSource_User);
          break;
        case ControlPoint::SurfacePointSource::AverageOfMeasures:
          protoPoint.set_apriorisurfpointsource(
                           ControlPointFileEntryV0002_AprioriSource_AverageOfMeasures);
          break;
        case ControlPoint::SurfacePointSource::Reference:
          protoPoint.set_apriorisurfpointsource(
                           ControlPointFileEntryV0002_AprioriSource_Reference);
          break;
        case ControlPoint::SurfacePointSource::Basemap:
          protoPoint.set_apriorisurfpointsource(ControlPointFileEntryV0002_AprioriSource_Basemap);
          break;
        case ControlPoint::SurfacePointSource::BundleSolution:
          protoPoint.set_apriorisurfpointsource(
                           ControlPointFileEntryV0002_AprioriSource_BundleSolution);
          break;
        default:
          protoPoint.set_apriorisurfpointsource(ControlPointFileEntryV0002_AprioriSource_None);
          break;
      }

      // Apriori Radius Point Source ENUM setting
      switch ( controlPoint->GetAprioriRadiusSource() ) {
        case ControlPoint::RadiusSource::None:
          protoPoint.set_aprioriradiussource(ControlPointFileEntryV0002_AprioriSource_None);
          break;
        case ControlPoint::RadiusSource::User:
          protoPoint.set_aprioriradiussource(ControlPointFileEntryV0002_AprioriSource_User);
          break;
        case ControlPoint::RadiusSource::AverageOfMeasures:
          protoPoint.set_aprioriradiussource(
                           ControlPointFileEntryV0002_AprioriSource_AverageOfMeasures);
          break;
        case ControlPoint::RadiusSource::BundleSolution:
          protoPoint.set_aprioriradiussource(
                           ControlPointFileEntryV0002_AprioriSource_BundleSolution);
          break;
        case ControlPoint::RadiusSource::Ellipsoid:
          protoPoint.set_aprioriradiussource(ControlPointFileEntryV0002_AprioriSource_Ellipsoid);
          break;
        case ControlPoint::RadiusSource::DEM:
          protoPoint.set_aprioriradiussource(ControlPointFileEntryV0002_AprioriSource_DEM);
          break;
        default:
          protoPoint.set_aprioriradiussource(ControlPointFileEntryV0002_AprioriSource_None);
          break;
      }

      if ( controlPoint->HasAprioriRadiusSourceFile() ) {
        protoPoint.set_aprioriradiussourcefile(
                   controlPoint->GetAprioriRadiusSourceFile().toLatin1().data());
      }

      SurfacePoint aprioriSurfacePoint = controlPoint->GetAprioriSurfacePoint();
      if ( aprioriSurfacePoint.Valid() ) {
        protoPoint.set_apriorix(aprioriSurfacePoint.GetX().meters());
        protoPoint.set_aprioriy(aprioriSurfacePoint.GetY().meters());
        protoPoint.set_aprioriz(aprioriSurfacePoint.GetZ().meters());

        symmetric_matrix<double, upper> aprioriCovarianceMatrix =
              aprioriSurfacePoint.GetRectangularMatrix();
        if ( aprioriCovarianceMatrix.size1() > 0 &&
             aprioriSurfacePoint.GetLatSigmaDistance().meters() != Isis::Null &&
             aprioriSurfacePoint.GetLonSigmaDistance().meters() != Isis::Null &&
             aprioriSurfacePoint.GetLocalRadiusSigma().meters() != Isis::Null ) {

          protoPoint.add_aprioricovar(aprioriCovarianceMatrix(0, 0));
          protoPoint.add_aprioricovar(aprioriCovarianceMatrix(0, 1));
          protoPoint.add_aprioricovar(aprioriCovarianceMatrix(0, 2));
          protoPoint.add_aprioricovar(aprioriCovarianceMatrix(1, 1));
          protoPoint.add_aprioricovar(aprioriCovarianceMatrix(1, 2));
          protoPoint.add_aprioricovar(aprioriCovarianceMatrix(2, 2));
        }
      }
      // this might be redundant... determined by covariance matrix???
      // *** TODO *** Address the generalized coordinates and the constraint differences
      if ( controlPoint->IsCoord1Constrained() ) {
        protoPoint.set_latitudeconstrained(controlPoint->IsCoord1Constrained());
      }
      if ( controlPoint->IsCoord2Constrained() ) {
        protoPoint.set_longitudeconstrained(controlPoint->IsCoord2Constrained());
      }
      if ( controlPoint->IsCoord3Constrained() ) {
        protoPoint.set_radiusconstrained(controlPoint->IsCoord3Constrained());
      }

      SurfacePoint adjustedSurfacePoint = controlPoint->GetAdjustedSurfacePoint();
      if ( adjustedSurfacePoint.Valid() ) {

        protoPoint.set_adjustedx(adjustedSurfacePoint.GetX().meters());
        protoPoint.set_adjustedy(adjustedSurfacePoint.GetY().meters());
        protoPoint.set_adjustedz(adjustedSurfacePoint.GetZ().meters());

        symmetric_matrix<double, upper> adjustedCovarianceMatrix =
              adjustedSurfacePoint.GetRectangularMatrix();
        if ( adjustedCovarianceMatrix.size1() > 0 ) {
          protoPoint.add_adjustedcovar(adjustedCovarianceMatrix(0, 0));
          protoPoint.add_adjustedcovar(adjustedCovarianceMatrix(0, 1));
          protoPoint.add_adjustedcovar(adjustedCovarianceMatrix(0, 2));
          protoPoint.add_adjustedcovar(adjustedCovarianceMatrix(1, 1));
          protoPoint.add_adjustedcovar(adjustedCovarianceMatrix(1, 2));
          protoPoint.add_adjustedcovar(adjustedCovarianceMatrix(2, 2));
        }
      }

      // Converting Measures
      for (int j = 0; j < controlPoint->GetNumMeasures(); j++) {

        const ControlMeasure &controlMeasure = *controlPoint->GetMeasure(j);

        ControlPointFileEntryV0002_Measure protoMeasure;

        protoMeasure.set_serialnumber(controlMeasure.GetCubeSerialNumber().toLatin1().data());

        switch ( controlMeasure.GetType() ) {
            case (ControlMeasure::Candidate):
                protoMeasure.set_type(ControlPointFileEntryV0002_Measure_MeasureType_Candidate);
                break;

            case (ControlMeasure::Manual):
                protoMeasure.set_type(ControlPointFileEntryV0002_Measure_MeasureType_Manual);
                break;

            case (ControlMeasure::RegisteredPixel):
                protoMeasure.set_type(
                      ControlPointFileEntryV0002_Measure_MeasureType_RegisteredPixel);
                break;

            case (ControlMeasure::RegisteredSubPixel):
                protoMeasure.set_type(
                      ControlPointFileEntryV0002_Measure_MeasureType_RegisteredSubPixel);
                break;
        }

        if (QString::compare(controlMeasure.GetChooserName(), "Null", Qt::CaseInsensitive) != 0) {
          protoMeasure.set_choosername(controlMeasure.GetChooserName().toLatin1().data());
        }

        if (QString::compare(controlMeasure.GetDateTime(), "Null", Qt::CaseInsensitive) != 0) {
          protoMeasure.set_datetime(controlMeasure.GetDateTime().toLatin1().data());
        }

        if ( controlMeasure.IsEditLocked() ) {
          protoMeasure.set_editlock(true);
        }

        if ( controlMeasure.IsIgnored() ) {
          protoMeasure.set_ignore(true);
        }

        if ( controlMeasure.IsRejected() ) {
          protoMeasure.set_jigsawrejected(true);
        }

        if ( controlMeasure.GetSample() != Isis::Null ) {
          protoMeasure.set_sample(controlMeasure.GetSample());
        }

        if ( controlMeasure.GetLine() != Isis::Null ) {
          protoMeasure.set_line(controlMeasure.GetLine());
        }

        if ( controlMeasure.GetDiameter() != Isis::Null ) {
          protoMeasure.set_diameter(controlMeasure.GetDiameter());
        }

        if ( controlMeasure.GetAprioriSample() != Isis::Null ) {
          protoMeasure.set_apriorisample(controlMeasure.GetAprioriSample());
        }

        if ( controlMeasure.GetAprioriLine() != Isis::Null ) {
          protoMeasure.set_aprioriline(controlMeasure.GetAprioriLine());
        }

        if ( controlMeasure.GetSampleSigma() != Isis::Null ) {
          protoMeasure.set_samplesigma(controlMeasure.GetSampleSigma());
        }

        if ( controlMeasure.GetLineSigma() != Isis::Null ) {
          protoMeasure.set_linesigma(controlMeasure.GetLineSigma());
        }

        if ( controlMeasure.GetSampleResidual() != Isis::Null ) {
          protoMeasure.set_sampleresidual(controlMeasure.GetSampleResidual());
        }

        if ( controlMeasure.GetLineResidual() != Isis::Null ) {
          protoMeasure.set_lineresidual(controlMeasure.GetLineResidual());
        }

        if ( controlMeasure.IsRejected() ) {
          protoMeasure.set_jigsawrejected(true);
        }

        QVector<ControlMeasureLogData> measureLogs = controlMeasure.GetLogDataEntries();
        for (int logEntry = 0; logEntry < measureLogs.size(); logEntry ++) {

          const ControlMeasureLogData &log = measureLogs[logEntry];

          ControlPointFileEntryV0002_Measure_MeasureLogData logData;

          if ( log.IsValid() ) {
            logData.set_doubledatatype( (int) log.GetDataType() );
            logData.set_doubledatavalue( log.GetNumericalValue() );
          }

          *protoMeasure.add_log() = logData;
        }

        *protoPoint.add_measures() = protoMeasure;
      }

      uint32_t byteSize = protoPoint.ByteSizeLong();

      Isis::EndianSwapper lsb("LSB");
      byteSize = lsb.Uint32_t(&byteSize);

      output->write(reinterpret_cast<char *>(&byteSize), sizeof(byteSize));

      if ( !protoPoint.SerializeToOstream(output) ) {
        QString err = "Error writing to coded protobuf stream";
        throw IException(IException::Programmer, err, _FILEINFO_);
      }

      // Make sure that if the versioner owns the ControlPoint it is properly cleaned up.
      if ( m_ownsPoints ) {
        delete controlPoint;
        controlPoint = NULL;
      }
      BigInt currentPos = output->tellp();
      BigInt byteCount = currentPos - startPos;

      // return size of message
      return byteCount;
  }


  /**
   * Read a control network from a Parquet file using GDAL.
   *
   * This method reads control network data from a Parquet file. The Parquet format
   * stores control points in a tabular format with separate layers for points and measures.
   *
   * @param netFile The Parquet file to read.
   * @param progress The progress object to track reading points.
   */
  void ControlNetVersioner::readParquet(const FileName netFile, Progress *progress) {
    cout << "reading parquert" << endl;
    
    GDALAllRegister();
    
    GDALDataset *poDS = (GDALDataset*) GDALOpenEx(
      netFile.expanded().toLatin1().data(),
      GDAL_OF_VECTOR | GDAL_OF_READONLY,
      NULL, NULL, NULL
    );
    
    cout << "created dataset" << endl;

    if (poDS == NULL) {
      QString msg = "Failed to open Parquet file [" + netFile.name() + "]";
      throw IException(IException::Io, msg, _FILEINFO_);
    }
    
    cout << "trying to read features" << endl; 

    try {
      // Read network metadata from "metadata" layer
      OGRLayer *metadataLayer = poDS->GetLayerByName("metadata");
      if (metadataLayer != NULL) {
        metadataLayer->ResetReading();
        OGRFeature *feature = metadataLayer->GetNextFeature();

        if (feature != NULL) {
          m_header.networkID = QString(feature->GetFieldAsString("networkId"));
          m_header.targetName = QString(feature->GetFieldAsString("targetName"));
          m_header.created = QString(feature->GetFieldAsString("created"));
          m_header.lastModified = QString(feature->GetFieldAsString("lastModified"));
          m_header.description = QString(feature->GetFieldAsString("description"));
          m_header.userName = QString(feature->GetFieldAsString("userName"));
          OGRFeature::DestroyFeature(feature);
        }
        else { 
          cout << "no metadata found" << endl;
          m_header.networkID = "networkId";
          m_header.targetName = "targetName";
          m_header.created = "created";
          m_header.lastModified = "lastModified";
          m_header.description = "description";
          m_header.userName = "userName";
        }
      }
      
      // Read control points from "points" layer
      OGRLayer *pointsLayer = poDS->GetLayer(0);
      if (pointsLayer == NULL) {
        GDALClose(poDS);
        QString msg = "Parquet file [" + netFile.name() + "] does not contain 'points' layer";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
      
      cout << "creating map of measures by point ID" << endl;

      // Build a map of measures by point ID for efficient lookup
      map<QString, QList<OGRFeature*>> measuresByPoint;
      pointsLayer->ResetReading();
      OGRFeature *measureFeature;
      while ((measureFeature = pointsLayer->GetNextFeature()) != NULL) {
        QString pointId = QString(measureFeature->GetFieldAsString("id"));
        if(pointId.isEmpty()) {
          throw IException(IException::Programmer, "Point ID is empty", _FILEINFO_);
        }
        cout << "adding measure feature to point ID: " << pointId << endl;
        measuresByPoint[pointId].append(measureFeature);
      }
      
      // Count points for progress
      GIntBig numPoints = pointsLayer->GetFeatureCount();

      
      for (const auto &pair : measuresByPoint) {
        try {
          cout << "creating control point with ID: " << pair.first << endl;
          ControlPoint *point = new ControlPoint();
          point->SetId(pair.first);
          
          point->SetChooserName(pair.second[0]->GetFieldAsString("chooserName"));
          point->SetDateTime(pair.second[0]->GetFieldAsString("dateTime"));
          point->SetEditLock(pair.second[0]->GetFieldAsInteger("editLock") != 0);
          point->SetIgnored(pair.second[0]->GetFieldAsInteger("ignore") != 0);
          
          // Read a priori surface point
          double aprioriX = pair.second[0]->GetFieldAsDouble("aprioriX");
          double aprioriY = pair.second[0]->GetFieldAsDouble("aprioriY");
          double aprioriZ = pair.second[0]->GetFieldAsDouble("aprioriZ");
              
          symmetric_matrix<double, upper> aprioriCovarianceMatrix;
          
          aprioriCovarianceMatrix.resize(3);
          aprioriCovarianceMatrix.clear();
          int size; 
          const double *aprioriCovar = pair.second[0]->GetFieldAsDoubleList("aprioriCovar", &size);
          if (size == 6) { 
            aprioriCovarianceMatrix(0, 0) = aprioriCovar[0];
            aprioriCovarianceMatrix(0, 1) = aprioriCovar[1];
            aprioriCovarianceMatrix(0, 2) = aprioriCovar[2];
            aprioriCovarianceMatrix(1, 1) = aprioriCovar[3];
            aprioriCovarianceMatrix(1, 2) = aprioriCovar[4];
            aprioriCovarianceMatrix(2, 2) = aprioriCovar[5];
            
            point->SetAprioriSurfacePoint(SurfacePoint(Displacement(aprioriX, Displacement::Meters),
                                                       Displacement(aprioriY, Displacement::Meters), 
                                                       Displacement(aprioriZ, Displacement::Meters), 
                                                       aprioriCovarianceMatrix));
          }
          else {
            cout << "apriori covariance matrix size is not a 6 element array, skipping" << endl;
            point->SetAprioriSurfacePoint(SurfacePoint(Displacement(aprioriX, Displacement::Meters),
                                                       Displacement(aprioriY, Displacement::Meters), 
                                                       Displacement(aprioriZ, Displacement::Meters)));
          }

    
          // read adjusted surface point
          double adjustedX = pair.second[0]->GetFieldAsDouble("adjustedx");
          double adjustedY = pair.second[0]->GetFieldAsDouble("adjustedy");
          double adjustedZ = pair.second[0]->GetFieldAsDouble("adjustedz");
            
          symmetric_matrix<double, upper> adjustedCovarianceMatrix;
          adjustedCovarianceMatrix.resize(3);
          adjustedCovarianceMatrix.clear();
          
          const double *adjustedCovar = pair.second[0]->GetFieldAsDoubleList("adjustedcovar", &size);
          if (size == 6) { 
            adjustedCovarianceMatrix(0, 0) = adjustedCovar[0];
            adjustedCovarianceMatrix(0, 1) = adjustedCovar[1];
            adjustedCovarianceMatrix(0, 2) = adjustedCovar[2];
            adjustedCovarianceMatrix(1, 1) = adjustedCovar[3];
            adjustedCovarianceMatrix(1, 2) = adjustedCovar[4];
            adjustedCovarianceMatrix(2, 2) = adjustedCovar[5];
            point->SetAdjustedSurfacePoint(SurfacePoint(Displacement(adjustedX, Displacement::Meters),
                                                       Displacement(adjustedY, Displacement::Meters), 
                                                       Displacement(adjustedZ, Displacement::Meters), 
                                                       adjustedCovarianceMatrix));
          }
          else {
            cout << "adjusted covariance matrix size is not a 6 element array, skipping" << endl;
            point->SetAdjustedSurfacePoint(SurfacePoint(Displacement(adjustedX, Displacement::Meters),
                                                       Displacement(adjustedY, Displacement::Meters), 
                                                       Displacement(adjustedZ, Displacement::Meters)));
          }
          
          // add measures to the point
          QList<OGRFeature*> measures = pair.second;
          for (OGRFeature *mfeature : measures) {
            ControlMeasure *measure = new ControlMeasure();
            
            // read serial number (required)
            QString serialnumber = QString(mfeature->GetFieldAsString("serialnumber"));
            measure->SetCubeSerialNumber(serialnumber);
            
            // read measure type
            int measureType = mfeature->GetFieldAsInteger("measureType");
            measure->SetType(static_cast<ControlMeasure::MeasureType>(measureType));
            
            // read sample and line
            if (mfeature->GetFieldIndex("sample") >= 0 && mfeature->GetFieldIndex("line") >= 0) {
              double sample = mfeature->GetFieldAsDouble("sample");
              double line = mfeature->GetFieldAsDouble("line");
              if (sample != Isis::Null && line != Isis::Null) {
                measure->SetCoordinate(sample, line);
              }
            }
            
            // read optional measure fields
            if (mfeature->GetFieldIndex("ignore") >= 0) {
              bool ignore = mfeature->GetFieldAsInteger("ignore") != 0;
              if (ignore) {
                measure->SetIgnored(true);
              }
            }
            
            if (mfeature->GetFieldIndex("editlock") >= 0) {
              bool editlock = mfeature->GetFieldAsInteger("editlock") != 0;
              if (editlock) {
                measure->SetEditLock(true);
              }
            }
            
            if (mfeature->GetFieldIndex("jigsawRejected") >= 0) {
              bool jigsawrejected = mfeature->GetFieldAsInteger("jigsawrejected") != 0;
              if (jigsawrejected) {
                measure->SetRejected(true);
              }
            }
            
            if (mfeature->GetFieldIndex("diameter") >= 0) {
              double diameter = mfeature->GetFieldAsDouble("diameter");
              if (diameter != Isis::Null) {
                measure->SetDiameter(diameter);
              }
            }
            
            if (mfeature->GetFieldIndex("apriorsample") >= 0) {
              double apriorsample = mfeature->GetFieldAsDouble("apriorsample");
              if (apriorsample != Isis::Null) {
                measure->SetAprioriSample(apriorsample);
              }
            }
            
            if (mfeature->GetFieldIndex("apriorline") >= 0) {
              double apriorline = mfeature->GetFieldAsDouble("apriorline");
              if (apriorline != Isis::Null) {
                measure->SetAprioriLine(apriorline);
              }
            }
            
            if (mfeature->GetFieldIndex("samplesigma") >= 0) {
              double samplesigma = mfeature->GetFieldAsDouble("samplesigma");
              if (samplesigma != Isis::Null) {
                measure->SetSampleSigma(samplesigma);
              }
            }
            
            if (mfeature->GetFieldIndex("linesigma") >= 0) {
              double linesigma = mfeature->GetFieldAsDouble("linesigma");
              if (linesigma != Isis::Null) {
                measure->SetLineSigma(linesigma);
              }
            }
            
            if (mfeature->GetFieldIndex("sampleResidual") >= 0 && 
                mfeature->GetFieldIndex("lineResidual") >= 0) {
              double sampleresidual = mfeature->GetFieldAsDouble("sampleResidual");
              double lineresidual = mfeature->GetFieldAsDouble("lineResidual");
              if (sampleresidual != Isis::Null && lineresidual != Isis::Null) {
                measure->SetResidual(sampleresidual, lineresidual);
              }
            }
            OGRFeature::DestroyFeature(mfeature);
            point->Add(measure);
          }
          
          m_points.append(point);
          cout << "added control point to list" << endl;
        }
        catch (IException &e) {
          throw e;
        }
      }
      cout << "closing parquet file" << endl;
      GDALClose(poDS);
    }
    catch (IException &e) {
      GDALClose(poDS);
      QString msg = "Failed to read Parquet control network file [" + netFile.name() + "]";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }
    cout << "done reading parquet file" << endl;
  }


  /**
   * Write a control network to a Parquet file using GDAL.
   *
   * This method writes control network data to a Parquet file. The Parquet format
   * stores control points in a tabular format with separate layers for metadata,
   * points, and measures.
   *
   * @param netFile The Parquet file to write.
   */
  void ControlNetVersioner::writeParquet(const FileName netFile) {
    GDALAllRegister();
    
    GDALDriver *poDriver = GetGDALDriverManager()->GetDriverByName("Parquet");
    if (poDriver == NULL) {
      QString msg = "Parquet driver not available in GDAL";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }
    
    GDALDataset *poDS = poDriver->Create(
      netFile.expanded().toLatin1().data(),
      0, 0, 0, GDT_Unknown, NULL
    );
    
    if (poDS == NULL) {
      QString msg = "Failed to create Parquet file [" + netFile.name() + "]";
      throw IException(IException::Io, msg, _FILEINFO_);
    }
    
    try {
      // Create metadata layer
      OGRLayer *metadataLayer = poDS->CreateLayer("metadata", NULL, wkbNone, NULL);
      if (metadataLayer == NULL) {
        GDALClose(poDS);
        QString msg = "Failed to create metadata layer in Parquet file";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
      
      OGRFieldDefn oFieldNetworkId("networkId", OFTString);
      OGRFieldDefn oFieldTargetName("targetName", OFTString);
      OGRFieldDefn oFieldCreated("created", OFTString);
      OGRFieldDefn oFieldLastModified("lastModified", OFTString);
      OGRFieldDefn oFieldDescription("description", OFTString);
      OGRFieldDefn oFieldUserName("userName", OFTString);
      
      metadataLayer->CreateField(&oFieldNetworkId);
      metadataLayer->CreateField(&oFieldTargetName);
      metadataLayer->CreateField(&oFieldCreated);
      metadataLayer->CreateField(&oFieldLastModified);
      metadataLayer->CreateField(&oFieldDescription);
      metadataLayer->CreateField(&oFieldUserName);
      
      OGRFeature *metadataFeature = OGRFeature::CreateFeature(metadataLayer->GetLayerDefn());
      metadataFeature->SetField("networkId", m_header.networkID.toLatin1().data());
      metadataFeature->SetField("targetName", m_header.targetName.toLatin1().data());
      metadataFeature->SetField("created", m_header.created.toLatin1().data());
      metadataFeature->SetField("lastModified", m_header.lastModified.toLatin1().data());
      metadataFeature->SetField("description", m_header.description.toLatin1().data());
      metadataFeature->SetField("userName", m_header.userName.toLatin1().data());
      
      if (metadataLayer->CreateFeature(metadataFeature) != OGRERR_NONE) {
        OGRFeature::DestroyFeature(metadataFeature);
        GDALClose(poDS);
        QString msg = "Failed to write metadata to Parquet file";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
      OGRFeature::DestroyFeature(metadataFeature);
      
      // Create points layer
      OGRLayer *pointsLayer = poDS->CreateLayer("points", NULL, wkbNone, NULL);
      if (pointsLayer == NULL) {
        GDALClose(poDS);
        QString msg = "Failed to create points layer in Parquet file";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
      
      // Define point fields
      OGRFieldDefn pointIdField("pointId", OFTString);
      OGRFieldDefn pointTypeField("pointType", OFTString);
      OGRFieldDefn chooserNameField("chooserName", OFTString);
      OGRFieldDefn dateTimeField("dateTime", OFTString);
      OGRFieldDefn editLockField("editLock", OFTInteger);
      OGRFieldDefn ignoreField("ignore", OFTInteger);
      OGRFieldDefn aprioriXField("aprioriX", OFTReal);
      OGRFieldDefn aprioriYField("aprioriY", OFTReal);
      OGRFieldDefn aprioriZField("aprioriZ", OFTReal);
      OGRFieldDefn aprioriCovar0Field("aprioriCovar0", OFTReal);
      OGRFieldDefn aprioriCovar1Field("aprioriCovar1", OFTReal);
      OGRFieldDefn aprioriCovar2Field("aprioriCovar2", OFTReal);
      OGRFieldDefn aprioriCovar3Field("aprioriCovar3", OFTReal);
      OGRFieldDefn aprioriCovar4Field("aprioriCovar4", OFTReal);
      OGRFieldDefn aprioriCovar5Field("aprioriCovar5", OFTReal);
      OGRFieldDefn adjustedXField("adjustedX", OFTReal);
      OGRFieldDefn adjustedYField("adjustedY", OFTReal);
      OGRFieldDefn adjustedZField("adjustedZ", OFTReal);
      OGRFieldDefn adjustedCovar0Field("adjustedCovar0", OFTReal);
      OGRFieldDefn adjustedCovar1Field("adjustedCovar1", OFTReal);
      OGRFieldDefn adjustedCovar2Field("adjustedCovar2", OFTReal);
      OGRFieldDefn adjustedCovar3Field("adjustedCovar3", OFTReal);
      OGRFieldDefn adjustedCovar4Field("adjustedCovar4", OFTReal);
      OGRFieldDefn adjustedCovar5Field("adjustedCovar5", OFTReal);
      
      pointsLayer->CreateField(&pointIdField);
      pointsLayer->CreateField(&pointTypeField);
      pointsLayer->CreateField(&chooserNameField);
      pointsLayer->CreateField(&dateTimeField);
      pointsLayer->CreateField(&editLockField);
      pointsLayer->CreateField(&ignoreField);
      pointsLayer->CreateField(&aprioriXField);
      pointsLayer->CreateField(&aprioriYField);
      pointsLayer->CreateField(&aprioriZField);
      pointsLayer->CreateField(&aprioriCovar0Field);
      pointsLayer->CreateField(&aprioriCovar1Field);
      pointsLayer->CreateField(&aprioriCovar2Field);
      pointsLayer->CreateField(&aprioriCovar3Field);
      pointsLayer->CreateField(&aprioriCovar4Field);
      pointsLayer->CreateField(&aprioriCovar5Field);
      pointsLayer->CreateField(&adjustedXField);
      pointsLayer->CreateField(&adjustedYField);
      pointsLayer->CreateField(&adjustedZField);
      pointsLayer->CreateField(&adjustedCovar0Field);
      pointsLayer->CreateField(&adjustedCovar1Field);
      pointsLayer->CreateField(&adjustedCovar2Field);
      pointsLayer->CreateField(&adjustedCovar3Field);
      pointsLayer->CreateField(&adjustedCovar4Field);
      pointsLayer->CreateField(&adjustedCovar5Field);
      
      // Create measures layer
      OGRLayer *measuresLayer = poDS->CreateLayer("measures", NULL, wkbNone, NULL);
      if (measuresLayer == NULL) {
        GDALClose(poDS);
        QString msg = "Failed to create measures layer in Parquet file";
        throw IException(IException::Io, msg, _FILEINFO_);
      }
      
      // Define measure fields
      OGRFieldDefn measurePointIdField("pointId", OFTString);
      OGRFieldDefn serialNumberField("serialNumber", OFTString);
      OGRFieldDefn measureTypeField("measureType", OFTString);
      OGRFieldDefn sampleField("sample", OFTReal);
      OGRFieldDefn lineField("line", OFTReal);
      OGRFieldDefn measureIgnoreField("ignore", OFTInteger);
      OGRFieldDefn measureEditLockField("editLock", OFTInteger);
      OGRFieldDefn jigsawRejectedField("jigsawRejected", OFTInteger);
      OGRFieldDefn diameterField("diameter", OFTReal);
      OGRFieldDefn apriorSampleField("apriorSample", OFTReal);
      OGRFieldDefn apriorLineField("apriorLine", OFTReal);
      OGRFieldDefn sampleSigmaField("sampleSigma", OFTReal);
      OGRFieldDefn lineSigmaField("lineSigma", OFTReal);
      OGRFieldDefn sampleResidualField("sampleResidual", OFTReal);
      OGRFieldDefn lineResidualField("lineResidual", OFTReal);
      
      measuresLayer->CreateField(&measurePointIdField);
      measuresLayer->CreateField(&serialNumberField);
      measuresLayer->CreateField(&measureTypeField);
      measuresLayer->CreateField(&sampleField);
      measuresLayer->CreateField(&lineField);
      measuresLayer->CreateField(&measureIgnoreField);
      measuresLayer->CreateField(&measureEditLockField);
      measuresLayer->CreateField(&jigsawRejectedField);
      measuresLayer->CreateField(&diameterField);
      measuresLayer->CreateField(&apriorSampleField);
      measuresLayer->CreateField(&apriorLineField);
      measuresLayer->CreateField(&sampleSigmaField);
      measuresLayer->CreateField(&lineSigmaField);
      measuresLayer->CreateField(&sampleResidualField);
      measuresLayer->CreateField(&lineResidualField);
      
      // Write control points
      foreach (ControlPoint *controlPoint, m_points) {
        OGRFeature *pointFeature = OGRFeature::CreateFeature(pointsLayer->GetLayerDefn());
        
        // Write required fields
        pointFeature->SetField("pointId", controlPoint->GetId().toLatin1().data());
        
        QString pointType;
        switch (controlPoint->GetType()) {
          case ControlPoint::Fixed:
            pointType = "Fixed";
            break;
          case ControlPoint::Constrained:
            pointType = "Constrained";
            break;
          default:
            pointType = "Free";
            break;
        }
        pointFeature->SetField("pointType", pointType.toLatin1().data());
        
        // Write optional fields
        if (QString::compare(controlPoint->GetChooserName(), "Null", Qt::CaseInsensitive) != 0) {
          pointFeature->SetField("chooserName", controlPoint->GetChooserName().toLatin1().data());
        }
        
        if (QString::compare(controlPoint->GetDateTime(), "Null", Qt::CaseInsensitive) != 0) {
          pointFeature->SetField("dateTime", controlPoint->GetDateTime().toLatin1().data());
        }
        
        pointFeature->SetField("editLock", controlPoint->IsEditLocked() ? 1 : 0);
        pointFeature->SetField("ignore", controlPoint->IsIgnored() ? 1 : 0);
        
        // Write a priori surface point
        SurfacePoint aprioriSurfacePoint = controlPoint->GetAprioriSurfacePoint();
        if (aprioriSurfacePoint.Valid()) {
          pointFeature->SetField("aprioriX", aprioriSurfacePoint.GetX().meters());
          pointFeature->SetField("aprioriY", aprioriSurfacePoint.GetY().meters());
          pointFeature->SetField("aprioriZ", aprioriSurfacePoint.GetZ().meters());
          
          symmetric_matrix<double, upper> aprioriCovarianceMatrix = 
            aprioriSurfacePoint.GetRectangularMatrix();
          if (aprioriCovarianceMatrix.size1() > 0) {
            pointFeature->SetField("aprioriCovar0", aprioriCovarianceMatrix(0, 0));
            pointFeature->SetField("aprioriCovar1", aprioriCovarianceMatrix(0, 1));
            pointFeature->SetField("aprioriCovar2", aprioriCovarianceMatrix(0, 2));
            pointFeature->SetField("aprioriCovar3", aprioriCovarianceMatrix(1, 1));
            pointFeature->SetField("aprioriCovar4", aprioriCovarianceMatrix(1, 2));
            pointFeature->SetField("aprioriCovar5", aprioriCovarianceMatrix(2, 2));
          }
        }
        
        // Write adjusted surface point
        SurfacePoint adjustedSurfacePoint = controlPoint->GetAdjustedSurfacePoint();
        if (adjustedSurfacePoint.Valid()) {
          pointFeature->SetField("adjustedX", adjustedSurfacePoint.GetX().meters());
          pointFeature->SetField("adjustedY", adjustedSurfacePoint.GetY().meters());
          pointFeature->SetField("adjustedZ", adjustedSurfacePoint.GetZ().meters());
          
          symmetric_matrix<double, upper> adjustedCovarianceMatrix = 
            adjustedSurfacePoint.GetRectangularMatrix();
          if (adjustedCovarianceMatrix.size1() > 0) {
            pointFeature->SetField("adjustedCovar0", adjustedCovarianceMatrix(0, 0));
            pointFeature->SetField("adjustedCovar1", adjustedCovarianceMatrix(0, 1));
            pointFeature->SetField("adjustedCovar2", adjustedCovarianceMatrix(0, 2));
            pointFeature->SetField("adjustedCovar3", adjustedCovarianceMatrix(1, 1));
            pointFeature->SetField("adjustedCovar4", adjustedCovarianceMatrix(1, 2));
            pointFeature->SetField("adjustedCovar5", adjustedCovarianceMatrix(2, 2));
          }
        }
        
        if (pointsLayer->CreateFeature(pointFeature) != OGRERR_NONE) {
          OGRFeature::DestroyFeature(pointFeature);
          GDALClose(poDS);
          QString msg = "Failed to write control point to Parquet file";
          throw IException(IException::Io, msg, _FILEINFO_);
        }
        OGRFeature::DestroyFeature(pointFeature);
        
        // Write measures for this point
        for (int j = 0; j < controlPoint->GetNumMeasures(); j++) {
          const ControlMeasure &controlMeasure = *controlPoint->GetMeasure(j);
          
          OGRFeature *measureFeature = OGRFeature::CreateFeature(measuresLayer->GetLayerDefn());
          
          measureFeature->SetField("pointId", controlPoint->GetId().toLatin1().data());
          measureFeature->SetField("serialNumber", controlMeasure.GetCubeSerialNumber().toLatin1().data());
          
          QString measureType;
          switch (controlMeasure.GetType()) {
            case ControlMeasure::Candidate:
              measureType = "Candidate";
              break;
            case ControlMeasure::Manual:
              measureType = "Manual";
              break;
            case ControlMeasure::RegisteredPixel:
              measureType = "RegisteredPixel";
              break;
            case ControlMeasure::RegisteredSubPixel:
              measureType = "RegisteredSubPixel";
              break;
          }
          measureFeature->SetField("measureType", measureType.toLatin1().data());
          
          if (controlMeasure.GetSample() != Isis::Null) {
            measureFeature->SetField("sample", controlMeasure.GetSample());
          }
          
          if (controlMeasure.GetLine() != Isis::Null) {
            measureFeature->SetField("line", controlMeasure.GetLine());
          }
          
          measureFeature->SetField("ignore", controlMeasure.IsIgnored() ? 1 : 0);
          measureFeature->SetField("editLock", controlMeasure.IsEditLocked() ? 1 : 0);
          measureFeature->SetField("jigsawRejected", controlMeasure.IsRejected() ? 1 : 0);
          
          if (controlMeasure.GetDiameter() != Isis::Null) {
            measureFeature->SetField("diameter", controlMeasure.GetDiameter());
          }
          
          if (controlMeasure.GetAprioriSample() != Isis::Null) {
            measureFeature->SetField("apriorSample", controlMeasure.GetAprioriSample());
          }
          
          if (controlMeasure.GetAprioriLine() != Isis::Null) {
            measureFeature->SetField("apriorLine", controlMeasure.GetAprioriLine());
          }
          
          if (controlMeasure.GetSampleSigma() != Isis::Null) {
            measureFeature->SetField("sampleSigma", controlMeasure.GetSampleSigma());
          }
          
          if (controlMeasure.GetLineSigma() != Isis::Null) {
            measureFeature->SetField("lineSigma", controlMeasure.GetLineSigma());
          }
          
          if (controlMeasure.GetSampleResidual() != Isis::Null) {
            measureFeature->SetField("sampleResidual", controlMeasure.GetSampleResidual());
          }
          
          if (controlMeasure.GetLineResidual() != Isis::Null) {
            measureFeature->SetField("lineResidual", controlMeasure.GetLineResidual());
          }
          
          if (measuresLayer->CreateFeature(measureFeature) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(measureFeature);
            GDALClose(poDS);
            QString msg = "Failed to write control measure to Parquet file";
            throw IException(IException::Io, msg, _FILEINFO_);
          }
          OGRFeature::DestroyFeature(measureFeature);
        }
      }
      
      GDALClose(poDS);
    }
    catch (IException &e) {
      GDALClose(poDS);
      QString msg = "Failed to write Parquet control network file [" + netFile.name() + "]";
      throw IException(e, IException::Io, msg, _FILEINFO_);
    }
    catch (...) {
      GDALClose(poDS);
      QString msg = "Unknown error writing Parquet control network file [" + netFile.name() + "]";
      throw IException(IException::Io, msg, _FILEINFO_);
    }
  }
}
