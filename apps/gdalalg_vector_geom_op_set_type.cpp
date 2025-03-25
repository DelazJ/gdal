/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  "gdal vector geom-op set-type"
 * Author:   Even Rouault <even dot rouault at spatialys.com>
 *
 ******************************************************************************
 * Copyright (c) 2025, Even Rouault <even dot rouault at spatialys.com>
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "gdalalg_vector_geom_op_set_type.h"

#include "gdal_priv.h"
#include "ogrsf_frmts.h"

//! @cond Doxygen_Suppress

#ifndef _
#define _(x) (x)
#endif

/************************************************************************/
/* GDALVectorGeomOpSetTypeAlgorithm::GDALVectorGeomOpSetTypeAlgorithm() */
/************************************************************************/

GDALVectorGeomOpSetTypeAlgorithm::GDALVectorGeomOpSetTypeAlgorithm(
    bool standaloneStep)
    : GDALVectorPipelineStepAlgorithm(NAME, DESCRIPTION, HELP_URL,
                                      standaloneStep)
{
    AddActiveLayerArg(&m_activeLayer);
    AddArg("active-geometry", 0,
           _("Geometry field name to which to restrict the processing (if not "
             "specified, all)"),
           &m_opts.m_geomField);

    AddArg("layer-only", 0, _("Only modify the layer geometry type"),
           &m_opts.m_layerOnly)
        .SetMutualExclusionGroup("only");
    AddArg("feature-only", 0, _("Only modify the geometry type of features"),
           &m_opts.m_featureGeomOnly)
        .SetMutualExclusionGroup("only");

    AddArg("geometry-type", 0, _("Geometry type"), &m_opts.m_type)
        .SetAutoCompleteFunction(
            [](const std::string &currentValue)
            {
                std::vector<std::string> oRet;
                for (const char *type :
                     {"GEOMETRY", "POINT", "LINESTRING", "POLYGON",
                      "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON",
                      "GEOMETRYCOLLECTION", "CURVE", "CIRCULARSTRING",
                      "COMPOUNDCURVE", "SURFACE", "CURVEPOLYGON", "MULTICURVE",
                      "MULTISURFACE", "POLYHEDRALSURFACE", "TIN"})
                {
                    if (currentValue.empty() ||
                        STARTS_WITH(type, currentValue.c_str()))
                    {
                        oRet.push_back(type);
                        oRet.push_back(std::string(type).append("Z"));
                        oRet.push_back(std::string(type).append("M"));
                        oRet.push_back(std::string(type).append("ZM"));
                    }
                }
                return oRet;
            });

    AddArg("multi", 0, _("Force geometries to MULTI geometry types"),
           &m_opts.m_multi)
        .SetMutualExclusionGroup("multi-single");
    AddArg("single", 0, _("Force geometries to non-MULTI geometry types"),
           &m_opts.m_single)
        .SetMutualExclusionGroup("multi-single");

    AddArg("linear", 0, _("Convert curve geometries to linear types"),
           &m_opts.m_linear)
        .SetMutualExclusionGroup("linear-curve");
    AddArg("curve", 0, _("Convert linear geometries to curve types"),
           &m_opts.m_curve)
        .SetMutualExclusionGroup("linear-curve");

    AddArg("xy", 0, _("Force geometries to XY dimension"), &m_opts.m_xy)
        .SetMutualExclusionGroup("xy");
    AddArg("xyz", 0, _("Force geometries to XYZ dimension"), &m_opts.m_xyz)
        .SetMutualExclusionGroup("xy");
    AddArg("xym", 0, _("Force geometries to XYM dimension"), &m_opts.m_xym)
        .SetMutualExclusionGroup("xy");
    AddArg("xyzm", 0, _("Force geometries to XYZM dimension"), &m_opts.m_xyzm)
        .SetMutualExclusionGroup("xy");

    AddArg("skip", 0,
           _("Skip feature when change of feature geometry type failed"),
           &m_opts.m_skip);
}

namespace
{

/************************************************************************/
/*               GDALVectorGeomOpSetTypeAlgorithmLayer                  */
/************************************************************************/

class GDALVectorGeomOpSetTypeAlgorithmLayer final
    : public GDALVectorPipelineOutputLayer
{
  private:
    const GDALVectorGeomOpSetTypeAlgorithm::Options m_opts;
    OGRFeatureDefn *m_poFeatureDefn = nullptr;

    CPL_DISALLOW_COPY_ASSIGN(GDALVectorGeomOpSetTypeAlgorithmLayer)

    std::unique_ptr<OGRFeature>
    TranslateFeature(std::unique_ptr<OGRFeature> poSrcFeature) const;

    void TranslateFeature(
        std::unique_ptr<OGRFeature> poSrcFeature,
        std::vector<std::unique_ptr<OGRFeature>> &apoOutFeatures) override
    {
        auto poDstFeature = TranslateFeature(std::move(poSrcFeature));
        if (poDstFeature)
            apoOutFeatures.push_back(std::move(poDstFeature));
    }

    OGRwkbGeometryType ConvertType(OGRwkbGeometryType eType) const;

  public:
    GDALVectorGeomOpSetTypeAlgorithmLayer(
        OGRLayer &oSrcLayer,
        const GDALVectorGeomOpSetTypeAlgorithm::Options &opts);

    ~GDALVectorGeomOpSetTypeAlgorithmLayer() override
    {
        m_poFeatureDefn->Dereference();
    }

    OGRFeatureDefn *GetLayerDefn() override
    {
        return m_poFeatureDefn;
    }

    GIntBig GetFeatureCount(int bForce) override
    {
        if (!m_opts.m_skip && !m_poAttrQuery && !m_poFilterGeom)
            return m_srcLayer.GetFeatureCount(bForce);
        return OGRLayer::GetFeatureCount(bForce);
    }

    OGRErr IGetExtent(int iGeomField, OGREnvelope *psExtent,
                      bool bForce) override
    {
        return m_srcLayer.GetExtent(iGeomField, psExtent, bForce);
    }

    OGRFeature *GetFeature(GIntBig nFID) override
    {
        auto poSrcFeature =
            std::unique_ptr<OGRFeature>(m_srcLayer.GetFeature(nFID));
        if (!poSrcFeature)
            return nullptr;
        return TranslateFeature(std::move(poSrcFeature)).release();
    }

    int TestCapability(const char *pszCap) override
    {
        if (EQUAL(pszCap, OLCRandomRead) || EQUAL(pszCap, OLCCurveGeometries) ||
            EQUAL(pszCap, OLCMeasuredGeometries) ||
            EQUAL(pszCap, OLCZGeometries) ||
            (EQUAL(pszCap, OLCFastFeatureCount) && !m_opts.m_skip &&
             !m_poAttrQuery && !m_poFilterGeom) ||
            EQUAL(pszCap, OLCFastGetExtent) || EQUAL(pszCap, OLCStringsAsUTF8))
        {
            return m_srcLayer.TestCapability(pszCap);
        }
        return false;
    }
};

/************************************************************************/
/*                 GDALVectorGeomOpSetTypeAlgorithmLayer()              */
/************************************************************************/

GDALVectorGeomOpSetTypeAlgorithmLayer::GDALVectorGeomOpSetTypeAlgorithmLayer(
    OGRLayer &oSrcLayer, const GDALVectorGeomOpSetTypeAlgorithm::Options &opts)
    : GDALVectorPipelineOutputLayer(oSrcLayer), m_opts(opts),
      m_poFeatureDefn(oSrcLayer.GetLayerDefn()->Clone())
{
    SetDescription(oSrcLayer.GetDescription());
    SetMetadata(oSrcLayer.GetMetadata());
    m_poFeatureDefn->Reference();

    if (!m_opts.m_featureGeomOnly)
    {
        for (int i = 0; i < m_poFeatureDefn->GetGeomFieldCount(); ++i)
        {
            auto poGeomFieldDefn = m_poFeatureDefn->GetGeomFieldDefn(i);
            if (m_opts.m_geomField.empty() ||
                m_opts.m_geomField == poGeomFieldDefn->GetNameRef())
            {
                poGeomFieldDefn->SetType(
                    ConvertType(poGeomFieldDefn->GetType()));
            }
        }
    }
}

/************************************************************************/
/*                           ConvertType()                             */
/************************************************************************/

OGRwkbGeometryType GDALVectorGeomOpSetTypeAlgorithmLayer::ConvertType(
    OGRwkbGeometryType eType) const
{
    if (!m_opts.m_type.empty())
        return m_opts.m_eType;

    OGRwkbGeometryType eRetType = eType;

    if (m_opts.m_multi)
    {
        if (eRetType == wkbTriangle || eRetType == wkbTIN ||
            eRetType == wkbPolyhedralSurface)
        {
            eRetType = wkbMultiPolygon;
        }
        else if (!OGR_GT_IsSubClassOf(eRetType, wkbGeometryCollection))
        {
            eRetType = OGR_GT_GetCollection(eRetType);
        }
    }
    else if (m_opts.m_single)
    {
        eRetType = OGR_GT_GetSingle(eRetType);
    }

    if (m_opts.m_linear)
    {
        eRetType = OGR_GT_GetLinear(eRetType);
    }
    else if (m_opts.m_curve)
    {
        eRetType = OGR_GT_GetCurve(eRetType);
    }

    if (m_opts.m_xy)
    {
        eRetType = OGR_GT_Flatten(eRetType);
    }
    else if (m_opts.m_xyz)
    {
        eRetType = OGR_GT_SetZ(OGR_GT_Flatten(eRetType));
    }
    else if (m_opts.m_xym)
    {
        eRetType = OGR_GT_SetM(OGR_GT_Flatten(eRetType));
    }
    else if (m_opts.m_xyzm)
    {
        eRetType = OGR_GT_SetZ(OGR_GT_SetM(OGR_GT_Flatten(eRetType)));
    }

    return eRetType;
}

/************************************************************************/
/*                          TranslateFeature()                          */
/************************************************************************/

std::unique_ptr<OGRFeature>
GDALVectorGeomOpSetTypeAlgorithmLayer::TranslateFeature(
    std::unique_ptr<OGRFeature> poSrcFeature) const
{
    poSrcFeature->SetFDefnUnsafe(m_poFeatureDefn);
    for (int i = 0; i < poSrcFeature->GetGeomFieldCount(); ++i)
    {
        auto poGeom = poSrcFeature->GetGeomFieldRef(i);
        if (poGeom)
        {
            const auto poGeomFieldDefn = m_poFeatureDefn->GetGeomFieldDefn(i);
            if (!m_opts.m_layerOnly &&
                (m_opts.m_geomField.empty() ||
                 m_opts.m_geomField == poGeomFieldDefn->GetNameRef()))
            {
                poGeom = poSrcFeature->StealGeometry(i);
                const auto eTargetType = ConvertType(poGeom->getGeometryType());
                auto poNewGeom = std::unique_ptr<OGRGeometry>(
                    OGRGeometryFactory::forceTo(poGeom, eTargetType));
                if (m_opts.m_skip &&
                    (!poNewGeom ||
                     (wkbFlatten(eTargetType) != wkbUnknown &&
                      poNewGeom->getGeometryType() != eTargetType)))
                {
                    return nullptr;
                }
                poNewGeom->assignSpatialReference(
                    poGeomFieldDefn->GetSpatialRef());
                poSrcFeature->SetGeomFieldDirectly(i, poNewGeom.release());
            }
            else
            {
                poGeom->assignSpatialReference(
                    poGeomFieldDefn->GetSpatialRef());
            }
        }
    }
    return poSrcFeature;
}

}  // namespace

/************************************************************************/
/*           GDALVectorGeomOpSetTypeAlgorithm::RunStep()                */
/************************************************************************/

bool GDALVectorGeomOpSetTypeAlgorithm::RunStep(GDALProgressFunc, void *)
{
    auto poSrcDS = m_inputDataset.GetDatasetRef();
    CPLAssert(poSrcDS);
    CPLAssert(m_outputDataset.GetName().empty());
    CPLAssert(!m_outputDataset.GetDatasetRef());

    auto outDS = std::make_unique<GDALVectorPipelineOutputDataset>(*poSrcDS);

    if (!m_opts.m_type.empty())
    {
        if (m_opts.m_multi || m_opts.m_single || m_opts.m_linear ||
            m_opts.m_curve || m_opts.m_xy || m_opts.m_xyz || m_opts.m_xym ||
            m_opts.m_xyzm)
        {
            ReportError(CE_Failure, CPLE_AppDefined,
                        "--geometry-type cannot be used with any of "
                        "--multi/single/linear/multi/xy/xyz/xym/xyzm");
            return false;
        }

        m_opts.m_eType = OGRFromOGCGeomType(m_opts.m_type.c_str());
        if (wkbFlatten(m_opts.m_eType) == wkbUnknown &&
            !STARTS_WITH_CI(m_opts.m_type.c_str(), "GEOMETRY"))
        {
            ReportError(CE_Failure, CPLE_AppDefined,
                        "Invalid geometry type '%s'", m_opts.m_type.c_str());
            return false;
        }
    }

    for (auto &&poSrcLayer : poSrcDS->GetLayers())
    {
        if (m_activeLayer.empty() ||
            m_activeLayer == poSrcLayer->GetDescription())
        {
            auto poLayer =
                std::make_unique<GDALVectorGeomOpSetTypeAlgorithmLayer>(
                    *poSrcLayer, m_opts);
            outDS->AddLayer(*poSrcLayer, std::move(poLayer));
        }
        else
        {
            outDS->AddLayer(
                *poSrcLayer,
                std::make_unique<GDALVectorPipelinePassthroughLayer>(
                    *poSrcLayer));
        }
    }

    m_outputDataset.Set(std::move(outDS));

    return true;
}

//! @endcond
