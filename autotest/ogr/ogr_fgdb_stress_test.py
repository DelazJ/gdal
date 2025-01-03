#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
#
# Project:  GDAL/OGR Test Suite
# Purpose:  FGDB driver stress testing of CreateFeature() with user set FID
# Author:   Even Rouault <even dot rouault at spatialys dot com>
#
###############################################################################
# Copyright (c) 2015, Even Rouault <even dot rouault at spatialys dot com>
#
# SPDX-License-Identifier: MIT
###############################################################################


import random
import shutil

import ogrtest
import pytest

from osgeo import gdal, ogr, osr

###############################################################################
# Test if driver is available


@pytest.mark.require_run_on_demand
def test_ogr_fgdb_stress_init():

    ogrtest.fgdb_drv = None
    ogrtest.openfilegdb_drv = None

    ogrtest.fgdb_drv = ogr.GetDriverByName("FileGDB")
    ogrtest.reference_drv = ogr.GetDriverByName("GPKG")
    ogrtest.reference_ext = "gpkg"
    ogrtest.openfilegdb_drv = ogr.GetDriverByName("OpenFileGDB")

    if ogrtest.fgdb_drv is None:
        pytest.skip()
    if ogrtest.reference_drv is None:
        pytest.skip()
    if ogrtest.openfilegdb_drv is None:
        pytest.skip()

    try:
        shutil.rmtree("tmp/test.gdb")
    except OSError:
        pass

    gdal.Unlink("tmp/test." + ogrtest.reference_ext)


###############################################################################
# Generate databases from random operations


@pytest.mark.require_run_on_demand
def test_ogr_fgdb_stress_1():
    if ogrtest.fgdb_drv is None:
        pytest.skip()

    verbose = False

    ds_test = ogrtest.fgdb_drv.CreateDataSource("tmp/test.gdb")
    ds_ref = ogrtest.reference_drv.CreateDataSource("tmp/test." + ogrtest.reference_ext)
    sr = osr.SpatialReference()
    sr.ImportFromEPSG(4326)
    lyr_test = ds_test.CreateLayer("test", geom_type=ogr.wkbPoint, srs=sr)
    lyr_ref = ds_ref.CreateLayer("test", geom_type=ogr.wkbPoint, srs=sr)
    for lyr in [lyr_test, lyr_ref]:
        lyr.CreateField(ogr.FieldDefn("str", ogr.OFTString))
    ds_test.ExecuteSQL("CREATE INDEX idx_test_str ON test(str)")
    ds_ref.ExecuteSQL("CREATE INDEX idx_test_str ON test(str)")
    random.seed(0)
    in_transaction = False
    nfeatures_created = 0
    for _ in range(100000):
        function = random.randrange(0, 500)
        if function == 0:
            if not in_transaction:
                if verbose:
                    print("StartTransaction")
                ds_test.StartTransaction(force=1)
            else:
                if verbose:
                    print("CommitTransaction")
                ds_test.CommitTransaction()
            in_transaction = not in_transaction
        elif function < 500 / 3:
            ret = []
            fid = -1
            if random.randrange(0, 2) == 0:
                fid = 1 + random.randrange(0, 1000)
            wkt = "POINT (%d %d)" % (random.randrange(0, 100), random.randrange(0, 100))
            if verbose:
                print("Create(%d)" % fid)
            for lyr in [lyr_test, lyr_ref]:
                f = ogr.Feature(lyr.GetLayerDefn())
                f.SetFID(fid)
                f.SetField(0, "%d" % random.randrange(0, 1000))
                f.SetGeometry(ogr.CreateGeometryFromWkt(wkt))
                with gdal.quiet_errors():
                    ret.append(lyr.CreateFeature(f))
                # So to ensure lyr_ref will use the same FID as the tested layer
                fid = f.GetFID()
                # print("created %d" % fid)
            assert ret[0] == ret[1]
            if ret[0] == 0:
                nfeatures_created += 1
        # For some odd reason, the .spx file is no longer updated when doing
        # a SetFeature() before having creating at least 2 features !
        elif function < 500 * 2 / 3 and nfeatures_created >= 2:
            ret = []
            fid = 1 + random.randrange(0, 1000)
            if verbose:
                print("Update(%d)" % fid)
            wkt = "POINT (%d %d)" % (random.randrange(0, 100), random.randrange(0, 100))
            for lyr in [lyr_test, lyr_ref]:
                f = ogr.Feature(lyr.GetLayerDefn())
                f.SetFID(fid)
                f.SetField(0, "%d" % random.randrange(0, 1000))
                f.SetGeometry(ogr.CreateGeometryFromWkt(wkt))
                # gdal.PushErrorHandler()
                ret.append(lyr.SetFeature(f))
                # gdal.PopErrorHandler()
            assert ret[0] == ret[1]
        # Same for DeleteFeature()
        elif nfeatures_created >= 2:
            ret = []
            fid = 1 + random.randrange(0, 1000)
            if verbose:
                print("Delete(%d)" % fid)
            for lyr in [lyr_test, lyr_ref]:
                # gdal.PushErrorHandler()
                ret.append(lyr.DeleteFeature(fid))
                # gdal.PopErrorHandler()
            assert ret[0] == ret[1]

    if in_transaction:
        ds_test.CommitTransaction()


###############################################################################
# Compare databases


@pytest.mark.require_run_on_demand
def test_ogr_fgdb_stress_2():
    if ogrtest.fgdb_drv is None:
        pytest.skip()

    ds_test = ogr.Open("tmp/test.gdb")
    ds_ref = ogr.Open("tmp/test." + ogrtest.reference_ext)

    lyr_test = ds_test.GetLayer(0)
    lyr_ref = ds_ref.GetLayer(0)

    while True:
        f_test = lyr_test.GetNextFeature()
        f_ref = lyr_ref.GetNextFeature()
        assert not (f_test is None and f_ref is not None) or (
            f_test is not None and f_ref is None
        )
        if f_test is None:
            break

        assert f_test.GetFID() == f_ref.GetFID()
        assert f_test["str"] == f_ref["str"]
        ogrtest.check_feature_geometry(f_test, f_ref.GetGeometryRef())

    for val in range(1000):
        lyr_test.SetAttributeFilter("str = '%d'" % val)
        lyr_ref.SetAttributeFilter("str = '%d'" % val)
        assert lyr_test.GetFeatureCount() == lyr_ref.GetFeatureCount(), val

    # sys.exit(0)


###############################################################################
# Cleanup


@pytest.mark.require_run_on_demand
def test_ogr_fgdb_stress_cleanup():
    if ogrtest.fgdb_drv is None:
        pytest.skip()

    try:
        shutil.rmtree("tmp/test.gdb")
    except OSError:
        pass

    gdal.Unlink("tmp/test." + ogrtest.reference_ext)
