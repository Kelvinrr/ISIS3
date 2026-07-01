/** This is free and unencumbered software released into the public domain.

The authors of ISIS do not claim copyright on the contents of this file.
For more details about the LICENSE terms and the AUTHORS, you will
find files of those names at the top level of this repository. **/

/* SPDX-License-Identifier: CC0-1.0 */

#include <QString>
#include <QTemporaryDir>

#include <boost/numeric/ublas/symmetric.hpp>

#include "ControlNet.h"
#include "ControlPoint.h"
#include "ControlMeasure.h"
#include "ControlMeasureLogData.h"
#include "Distance.h"
#include "Displacement.h"
#include "FileName.h"
#include "IException.h"
#include "SpecialPixel.h"
#include "SurfacePoint.h"

#include "TempFixtures.h"
#include "gmock/gmock.h"

using namespace Isis;

/**
 * Tests for Parquet control network support in ControlNetVersioner.
 *
 * The Parquet path bridges through the same ControlPointFileEntryV0002 protobuf
 * message as the binary (.net) path, so these tests assert that a control network
 * written to Parquet and read back is identical to the same network written to and
 * read from protobuf -- i.e. Parquet is a lossless, additive format that does not
 * disturb backwards compatibility.
 */
namespace {

  /**
   * Build a small but field-diverse control network in memory:
   *   - a Free point with apriori + adjusted surface points (with covariance) and
   *     two measures (one ignored, one fully populated with residuals/sigmas/logs)
   *   - a Constrained point with one measure
   *   - a Fixed point with no measures (exercises the zero-measure row path)
   */
  ControlNet *buildTestNetwork() {
    ControlNet *net = new ControlNet();
    net->SetNetworkId("TestParquetNet");
    net->SetTarget("Mars");
    net->SetUserName("tester");
    net->SetDescription("Parquet round-trip test network");

    // --- Point 1: Free, with apriori + adjusted surface points -------------
    ControlPoint *p1 = new ControlPoint("p1");
    p1->SetType(ControlPoint::Free);
    p1->SetChooserName("chooser1");
    p1->SetAprioriRadiusSource(ControlPoint::RadiusSource::DEM);
    p1->SetAprioriSurfacePointSource(ControlPoint::SurfacePointSource::Reference);

    boost::numeric::ublas::symmetric_matrix<double, boost::numeric::ublas::upper> covar(3);
    covar(0, 0) = 100.0; covar(0, 1) = 1.0;  covar(0, 2) = 2.0;
    covar(1, 1) = 200.0; covar(1, 2) = 3.0;
    covar(2, 2) = 300.0;
    SurfacePoint apriori(Displacement(1000.0, Displacement::Meters),
                         Displacement(2000.0, Displacement::Meters),
                         Displacement(3000.0, Displacement::Meters),
                         covar);
    p1->SetAprioriSurfacePoint(apriori);

    SurfacePoint adjusted(Displacement(1001.0, Displacement::Meters),
                          Displacement(2002.0, Displacement::Meters),
                          Displacement(3003.0, Displacement::Meters),
                          covar);
    p1->SetAdjustedSurfacePoint(adjusted);

    ControlMeasure *m1 = new ControlMeasure();
    m1->SetCubeSerialNumber("SN_A");
    m1->SetCoordinate(10.5, 20.5, ControlMeasure::RegisteredSubPixel);
    m1->SetResidual(0.1, 0.2);
    m1->SetChooserName("mchooser");
    m1->SetDiameter(5.0);
    m1->SetAprioriSample(10.0);
    m1->SetAprioriLine(20.0);
    m1->SetLogData(ControlMeasureLogData(ControlMeasureLogData::GoodnessOfFit, 0.95));
    p1->Add(m1);

    ControlMeasure *m2 = new ControlMeasure();
    m2->SetCubeSerialNumber("SN_B");
    m2->SetCoordinate(30.0, 40.0, ControlMeasure::Candidate);
    m2->SetIgnored(true);
    p1->Add(m2);

    p1->SetRefMeasure(m1);
    net->AddPoint(p1);

    // --- Point 2: Constrained, single measure ------------------------------
    ControlPoint *p2 = new ControlPoint("p2");
    p2->SetType(ControlPoint::Constrained);
    ControlMeasure *m3 = new ControlMeasure();
    m3->SetCubeSerialNumber("SN_A");
    m3->SetCoordinate(50.0, 60.0, ControlMeasure::Manual);
    p2->Add(m3);
    net->AddPoint(p2);

    // --- Point 3: Fixed, zero measures -------------------------------------
    ControlPoint *p3 = new ControlPoint("p3");
    p3->SetType(ControlPoint::Fixed);
    net->AddPoint(p3);

    return net;
  }

  /**
   * Assert that two control networks are equal at the field level that the
   * file format is responsible for round-tripping.
   */
  void expectNetworksEqual(ControlNet *a, ControlNet *b) {
    ASSERT_EQ(a->GetNumPoints(), b->GetNumPoints());

    for (int i = 0; i < a->GetNumPoints(); i++) {
      ControlPoint *pa = a->GetPoint(i);
      ControlPoint *pb = b->GetPoint(pa->GetId());
      ASSERT_NE(pb, nullptr) << "point " << pa->GetId().toStdString() << " missing after round-trip";

      EXPECT_EQ(pa->GetType(), pb->GetType());
      EXPECT_EQ(pa->GetNumMeasures(), pb->GetNumMeasures());
      EXPECT_EQ(pa->IsEditLocked(), pb->IsEditLocked());
      EXPECT_EQ(pa->IsIgnored(), pb->IsIgnored());

      // Apriori / adjusted surface points (incl. covariance) compared in meters.
      SurfacePoint aAp = pa->GetAprioriSurfacePoint();
      SurfacePoint bAp = pb->GetAprioriSurfacePoint();
      EXPECT_EQ(aAp.Valid(), bAp.Valid());
      if (aAp.Valid() && bAp.Valid()) {
        EXPECT_NEAR(aAp.GetX().meters(), bAp.GetX().meters(), 1e-9);
        EXPECT_NEAR(aAp.GetY().meters(), bAp.GetY().meters(), 1e-9);
        EXPECT_NEAR(aAp.GetZ().meters(), bAp.GetZ().meters(), 1e-9);
        auto am = aAp.GetRectangularMatrix();
        auto bm = bAp.GetRectangularMatrix();
        ASSERT_EQ(am.size1(), bm.size1());
        for (size_t r = 0; r < am.size1(); r++) {
          for (size_t c = r; c < am.size2(); c++) {
            EXPECT_NEAR(am(r, c), bm(r, c), 1e-9);
          }
        }
      }

      SurfacePoint aAd = pa->GetAdjustedSurfacePoint();
      SurfacePoint bAd = pb->GetAdjustedSurfacePoint();
      EXPECT_EQ(aAd.Valid(), bAd.Valid());
      if (aAd.Valid() && bAd.Valid()) {
        EXPECT_NEAR(aAd.GetX().meters(), bAd.GetX().meters(), 1e-9);
        EXPECT_NEAR(aAd.GetY().meters(), bAd.GetY().meters(), 1e-9);
        EXPECT_NEAR(aAd.GetZ().meters(), bAd.GetZ().meters(), 1e-9);
      }

      // Reference measure index round-trips (only meaningful for non-empty points).
      if (pa->GetNumMeasures() > 0) {
        EXPECT_EQ(pa->IndexOfRefMeasure(), pb->IndexOfRefMeasure());
      }

      for (int j = 0; j < pa->GetNumMeasures(); j++) {
        ControlMeasure *ma = pa->GetMeasure(j);
        ControlMeasure *mb = pb->GetMeasure(ma->GetCubeSerialNumber());
        ASSERT_NE(mb, nullptr);
        EXPECT_EQ(ma->GetType(), mb->GetType());
        EXPECT_EQ(ma->IsIgnored(), mb->IsIgnored());
        EXPECT_NEAR(ma->GetSample(), mb->GetSample(), 1e-9);
        EXPECT_NEAR(ma->GetLine(), mb->GetLine(), 1e-9);
        if (ma->GetSampleResidual() != Isis::Null) {
          EXPECT_NEAR(ma->GetSampleResidual(), mb->GetSampleResidual(), 1e-9);
          EXPECT_NEAR(ma->GetLineResidual(), mb->GetLineResidual(), 1e-9);
        }
        EXPECT_EQ(ma->GetLogDataEntries().size(), mb->GetLogDataEntries().size());
      }
    }
  }
}


/**
 * The Parquet round-trip equals the protobuf round-trip for the same in-memory
 * network. This is the core lossless / backwards-compatible guarantee.
 */
TEST_F(TempTestingFiles, ControlNetVersionerParquetMatchesProtobuf) {
  ControlNet *original = buildTestNetwork();

  QString binPath = tempDir.path() + "/net.net";
  QString pqPath  = tempDir.path() + "/net.parquet";

  original->Write(binPath);    // protobuf (default)
  original->Write(pqPath);     // parquet (.parquet extension)

  ControlNet fromBin;
  ControlNet fromPq;
  ASSERT_NO_THROW(fromBin.ReadControl(binPath)) << "protobuf read failed";
  ASSERT_NO_THROW(fromPq.ReadControl(pqPath))   << "parquet read failed";

  // Parquet round-trip must match the (authoritative) protobuf round-trip.
  expectNetworksEqual(&fromBin, &fromPq);

  // And both must match the original.
  expectNetworksEqual(original, &fromPq);

  delete original;
}


/**
 * Parity test starting from a real on-disk protobuf control network:
 * read the protobuf .net, convert it to Parquet, then read both back. The two
 * in-memory networks must be identical -- proving Parquet and protobuf networks
 * carry the same data.
 */
TEST_F(TempTestingFiles, ControlNetVersionerParquetProtobufFileParity) {
  // A binary-protobuf (Object = ProtoBuffer) V5 network shipped with the tests.
  QString protoPath = "data/vikingThemisNetwork/themis_dayir_VO_arcadia_extract_hand.net";

  ControlNet fromProto;
  ASSERT_NO_THROW(fromProto.ReadControl(protoPath))
      << "failed to read source protobuf control network";
  ASSERT_GT(fromProto.GetNumPoints(), 0) << "source network is empty";

  // Convert the protobuf network to Parquet, then read it back.
  QString pqPath = tempDir.path() + "/converted.parquet";
  ASSERT_NO_THROW(fromProto.Write(pqPath)) << "failed to write Parquet network";

  ControlNet fromParquet;
  ASSERT_NO_THROW(fromParquet.ReadControl(pqPath)) << "failed to read Parquet network";

  // The Parquet network must match the protobuf network field-for-field.
  expectNetworksEqual(&fromProto, &fromParquet);
}


/**
 * Network header round-trips through the Parquet metadata columns.
 */
TEST_F(TempTestingFiles, ControlNetVersionerParquetHeader) {
  ControlNet *original = buildTestNetwork();
  QString pqPath = tempDir.path() + "/hdr.parquet";
  original->Write(pqPath);

  ControlNet fromPq;
  ASSERT_NO_THROW(fromPq.ReadControl(pqPath));
  EXPECT_EQ(fromPq.GetNetworkId(), QString("TestParquetNet"));
  EXPECT_EQ(fromPq.GetUserName(), QString("tester"));
  EXPECT_EQ(fromPq.Description(), QString("Parquet round-trip test network"));

  delete original;
}


/**
 * Requesting Pvl output to a .parquet path is a contradiction and must throw,
 * rather than silently ignoring the explicit pvl=true request.
 */
TEST_F(TempTestingFiles, ControlNetVersionerParquetPvlConflictThrows) {
  ControlNet *original = buildTestNetwork();
  QString pqPath = tempDir.path() + "/conflict.parquet";

  EXPECT_THROW(original->Write(pqPath, true), IException);

  delete original;
}


/**
 * Parquet read/write works over a GDAL VSI path (in-memory /vsimem), confirming
 * the user-supplied path reaches GDAL unmodified.
 */
TEST_F(TempTestingFiles, ControlNetVersionerParquetVsimem) {
  ControlNet *original = buildTestNetwork();

  QString vsiPath = "/vsimem/cnet_test.parquet";
  ASSERT_NO_THROW(original->Write(vsiPath));

  ControlNet fromVsi;
  ASSERT_NO_THROW(fromVsi.ReadControl(vsiPath));
  expectNetworksEqual(original, &fromVsi);

  delete original;
}
