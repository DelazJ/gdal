#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
# Project:  GDAL/OGR Test Suite
# Purpose:  'gdal raster stack' testing
# Author:  Daniel Baston
#
###############################################################################
# Copyright (c) 2025, ISciences LLC
#
# SPDX-License-Identifier: MIT
###############################################################################

import pytest

from osgeo import gdal


@pytest.fixture()
def stack():
    return gdal.GetGlobalAlgorithmRegistry()["raster"]["stack"]


def test_gdalalg_raster_stack_resolution_common(stack):

    # resolution = 3
    src1_ds = gdal.GetDriverByName("MEM").Create("", 5, 5)
    src1_ds.SetGeoTransform([2, 3, 0, 49, 0, -3])

    # resolution = 5
    src2_ds = gdal.GetDriverByName("MEM").Create("", 3, 3)
    src2_ds.SetGeoTransform([2, 5, 0, 49, 0, -5])

    stack["input"] = [src1_ds, src2_ds]
    stack["output"] = ""
    stack["resolution"] = "common"
    assert stack.Run()
    ds = stack["output"].GetDataset()

    assert ds.RasterCount == 2
    assert ds.RasterXSize == 15
    assert ds.RasterYSize == 15
    assert ds.GetGeoTransform() == pytest.approx((2.0, 1.0, 0.0, 49.0, 0.0, -1.0))
