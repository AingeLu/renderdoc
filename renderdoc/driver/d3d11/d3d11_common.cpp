/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2017 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "d3d11_common.h"
#include "core/core.h"
#include "driver/d3d11/d3d11_renderstate.h"
#include "driver/d3d11/d3d11_resources.h"
#include "serialise/serialiser.h"
#include "strings/string_utils.h"

WrappedID3D11Device *D3D11MarkerRegion::device;

D3D11MarkerRegion::D3D11MarkerRegion(const std::string &marker)
{
  D3D11MarkerRegion::Begin(marker);
}

D3D11MarkerRegion::~D3D11MarkerRegion()
{
  D3D11MarkerRegion::End();
}

void D3D11MarkerRegion::Set(const std::string &marker)
{
  if(device == NULL)
    return;

  ID3DUserDefinedAnnotation *annot = device->GetAnnotations();

  if(annot)
    annot->SetMarker(StringFormat::UTF82Wide(marker).c_str());
}

void D3D11MarkerRegion::Begin(const std::string &marker)
{
  if(device == NULL)
    return;

  ID3DUserDefinedAnnotation *annot = device->GetAnnotations();

  if(annot)
    annot->BeginEvent(StringFormat::UTF82Wide(marker).c_str());
}

void D3D11MarkerRegion::End()
{
  if(device == NULL)
    return;

  ID3DUserDefinedAnnotation *annot = device->GetAnnotations();

  if(annot)
    annot->EndEvent();
}

ResourceRange ResourceRange::Null = ResourceRange(ResourceRange::empty);

ResourceRange::ResourceRange(ID3D11ShaderResourceView *srv)
{
  minMip = minSlice = 0;
  depthReadOnly = false;
  stencilReadOnly = false;

  if(srv == NULL)
  {
    resource = NULL;
    maxMip = allMip;
    maxSlice = allSlice;
    fullRange = true;
    return;
  }

// in non-release make sure we always consistently check wrapped resources/views. Otherwise we could
// end up in a situation where we're comparing two resource ranges that do overlap, but one was
// constructed with the wrapped view and one wasn't - so they compare differently
#if ENABLED(RDOC_DEVEL)
  RDCASSERT(WrappedID3D11ShaderResourceView1::IsAlloc(srv));
#endif

  ID3D11Resource *res = NULL;
  srv->GetResource(&res);
  res->Release();
  resource = (IUnknown *)res;

  UINT numMips = allMip, numSlices = allSlice;

  D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
  srv->GetDesc(&srvd);

  // extract depth/stencil read only flags if appropriate
  {
    D3D11_RESOURCE_DIMENSION dim;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

    fmt = srvd.Format;

    res->GetType(&dim);

    if(fmt == DXGI_FORMAT_UNKNOWN)
    {
      if(dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
      {
        D3D11_TEXTURE1D_DESC d;
        ((ID3D11Texture1D *)res)->GetDesc(&d);

        fmt = d.Format;
      }
      else if(dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
      {
        D3D11_TEXTURE2D_DESC d;
        ((ID3D11Texture2D *)res)->GetDesc(&d);

        fmt = d.Format;
      }
    }

    if(fmt == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT || fmt == DXGI_FORMAT_X24_TYPELESS_G8_UINT)
    {
      stencilReadOnly = true;
    }
    else if(fmt == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS || fmt == DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
    {
      depthReadOnly = true;
    }
    else
    {
      fmt = GetTypelessFormat(fmt);

      // any format that could be depth-only, treat it as reading depth only.
      // this only applies for conflicts detected with the depth target.
      if(fmt == DXGI_FORMAT_R32_TYPELESS || fmt == DXGI_FORMAT_R16_TYPELESS)
      {
        depthReadOnly = true;
      }
    }
  }

  switch(srvd.ViewDimension)
  {
    case D3D11_SRV_DIMENSION_TEXTURE1D:
      minMip = srvd.Texture1D.MostDetailedMip;
      numMips = srvd.Texture1D.MipLevels;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
      minMip = srvd.Texture1DArray.MostDetailedMip;
      numMips = srvd.Texture1DArray.MipLevels;
      minSlice = srvd.Texture1DArray.FirstArraySlice;
      numSlices = srvd.Texture1DArray.ArraySize;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2D:
      minMip = srvd.Texture2D.MostDetailedMip;
      numMips = srvd.Texture2D.MipLevels;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
      minMip = srvd.Texture2DArray.MostDetailedMip;
      numMips = srvd.Texture2DArray.MipLevels;
      minSlice = srvd.Texture2DArray.FirstArraySlice;
      numSlices = srvd.Texture2DArray.ArraySize;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS: break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
      minSlice = srvd.Texture2DMSArray.FirstArraySlice;
      numSlices = srvd.Texture2DMSArray.ArraySize;
      break;
    case D3D11_SRV_DIMENSION_TEXTURE3D:
      minMip = srvd.Texture3D.MostDetailedMip;
      numMips = srvd.Texture3D.MipLevels;
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
      minMip = srvd.TextureCube.MostDetailedMip;
      numMips = srvd.TextureCube.MipLevels;
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
      minMip = srvd.TextureCubeArray.MostDetailedMip;
      numMips = srvd.TextureCubeArray.MipLevels;
      minSlice = srvd.TextureCubeArray.First2DArrayFace;
      numSlices = srvd.TextureCubeArray.NumCubes * 6;
      break;
    case D3D11_SRV_DIMENSION_UNKNOWN:
    case D3D11_SRV_DIMENSION_BUFFER:
    case D3D11_SRV_DIMENSION_BUFFEREX: break;
  }

  SetMaxes(numMips, numSlices);
}

ResourceRange::ResourceRange(ID3D11UnorderedAccessView *uav)
{
  minMip = minSlice = 0;
  depthReadOnly = false;
  stencilReadOnly = false;

  if(uav == NULL)
  {
    resource = NULL;
    maxMip = allMip;
    maxSlice = allSlice;
    fullRange = true;
    return;
  }

#if ENABLED(RDOC_DEVEL)
  RDCASSERT(WrappedID3D11UnorderedAccessView1::IsAlloc(uav));
#endif

  ID3D11Resource *res = NULL;
  uav->GetResource(&res);
  res->Release();
  resource = (IUnknown *)res;

  UINT numMips = allMip, numSlices = allSlice;

  D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
  uav->GetDesc(&desc);

  switch(desc.ViewDimension)
  {
    case D3D11_UAV_DIMENSION_TEXTURE1D:
      minMip = desc.Texture1D.MipSlice;
      numMips = 1;
      break;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
      minMip = desc.Texture1DArray.MipSlice;
      numMips = 1;
      minSlice = desc.Texture1DArray.FirstArraySlice;
      numSlices = desc.Texture1DArray.ArraySize;
      break;
    case D3D11_UAV_DIMENSION_TEXTURE2D:
      minMip = desc.Texture2D.MipSlice;
      numMips = 1;
      break;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
      minMip = desc.Texture2DArray.MipSlice;
      numMips = 1;
      minSlice = desc.Texture2DArray.FirstArraySlice;
      numSlices = desc.Texture2DArray.ArraySize;
      break;
    case D3D11_UAV_DIMENSION_TEXTURE3D:
      minMip = desc.Texture3D.MipSlice;
      numMips = 1;
      minSlice = desc.Texture3D.FirstWSlice;
      numSlices = desc.Texture3D.WSize;
      break;
    case D3D11_UAV_DIMENSION_UNKNOWN:
    case D3D11_UAV_DIMENSION_BUFFER: break;
  }

  SetMaxes(numMips, numSlices);
}

ResourceRange::ResourceRange(ID3D11RenderTargetView *rtv)
{
  minMip = minSlice = 0;
  depthReadOnly = false;
  stencilReadOnly = false;

  if(rtv == NULL)
  {
    resource = NULL;
    maxMip = allMip;
    maxSlice = allSlice;
    fullRange = true;
    return;
  }

#if ENABLED(RDOC_DEVEL)
  RDCASSERT(WrappedID3D11RenderTargetView1::IsAlloc(rtv));
#endif

  ID3D11Resource *res = NULL;
  rtv->GetResource(&res);
  res->Release();
  resource = (IUnknown *)res;

  UINT numMips = allMip, numSlices = allSlice;

  D3D11_RENDER_TARGET_VIEW_DESC desc;
  rtv->GetDesc(&desc);

  switch(desc.ViewDimension)
  {
    case D3D11_RTV_DIMENSION_TEXTURE1D:
      minMip = desc.Texture1D.MipSlice;
      numMips = 1;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
      minMip = desc.Texture1DArray.MipSlice;
      numMips = 1;
      minSlice = desc.Texture1DArray.FirstArraySlice;
      numSlices = desc.Texture1DArray.ArraySize;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2D:
      minMip = desc.Texture2D.MipSlice;
      numMips = 1;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
      minMip = desc.Texture2DArray.MipSlice;
      numMips = 1;
      minSlice = desc.Texture2DArray.FirstArraySlice;
      numSlices = desc.Texture2DArray.ArraySize;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS: break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
      minSlice = desc.Texture2DMSArray.FirstArraySlice;
      numSlices = desc.Texture2DMSArray.ArraySize;
      break;
    case D3D11_RTV_DIMENSION_TEXTURE3D:
      minMip = desc.Texture3D.MipSlice;
      numMips = 1;
      minSlice = desc.Texture3D.FirstWSlice;
      numSlices = desc.Texture3D.WSize;
      break;
    case D3D11_RTV_DIMENSION_UNKNOWN:
    case D3D11_RTV_DIMENSION_BUFFER: break;
  }

  SetMaxes(numMips, numSlices);
}

ResourceRange::ResourceRange(ID3D11DepthStencilView *dsv)
{
  minMip = minSlice = 0;
  depthReadOnly = false;
  stencilReadOnly = false;

  if(dsv == NULL)
  {
    resource = NULL;
    maxMip = allMip;
    maxSlice = allSlice;
    fullRange = true;
    return;
  }

#if ENABLED(RDOC_DEVEL)
  RDCASSERT(WrappedID3D11DepthStencilView::IsAlloc(dsv));
#endif

  ID3D11Resource *res = NULL;
  dsv->GetResource(&res);
  res->Release();
  resource = (IUnknown *)res;

  UINT numMips = allMip, numSlices = allSlice;

  D3D11_DEPTH_STENCIL_VIEW_DESC desc;
  dsv->GetDesc(&desc);

  if(desc.Flags & D3D11_DSV_READ_ONLY_DEPTH)
    depthReadOnly = true;
  if(desc.Flags & D3D11_DSV_READ_ONLY_STENCIL)
    stencilReadOnly = true;

  switch(desc.ViewDimension)
  {
    case D3D11_DSV_DIMENSION_TEXTURE1D:
      minMip = desc.Texture1D.MipSlice;
      numMips = 1;
      break;
    case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
      minMip = desc.Texture1DArray.MipSlice;
      numMips = 1;
      minSlice = desc.Texture1DArray.FirstArraySlice;
      numSlices = desc.Texture1DArray.ArraySize;
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2D:
      minMip = desc.Texture2D.MipSlice;
      numMips = 1;
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
      minMip = desc.Texture2DArray.MipSlice;
      numMips = 1;
      minSlice = desc.Texture2DArray.FirstArraySlice;
      numSlices = desc.Texture2DArray.ArraySize;
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS: break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
      minSlice = desc.Texture2DMSArray.FirstArraySlice;
      numSlices = desc.Texture2DMSArray.ArraySize;
      break;
    case D3D11_DSV_DIMENSION_UNKNOWN: break;
  }

  SetMaxes(numMips, numSlices);
}

ResourceRange::ResourceRange(ID3D11Buffer *res)
{
#if ENABLED(RDOC_DEVEL)
  RDCASSERT(!res || WrappedID3D11Buffer::IsAlloc(res));
#endif

  resource = res;
  minMip = minSlice = 0;
  maxMip = allMip;
  maxSlice = allSlice;
  fullRange = true;
  depthReadOnly = false;
  stencilReadOnly = false;
}

ResourceRange::ResourceRange(ID3D11Texture2D *res)
{
#if ENABLED(RDOC_DEVEL)
  RDCASSERT(!res || WrappedID3D11Texture2D1::IsAlloc(res));
#endif

  resource = res;
  minMip = minSlice = 0;
  maxMip = allMip;
  maxSlice = allSlice;
  fullRange = true;
  depthReadOnly = false;
  stencilReadOnly = false;
}

ResourceRange::ResourceRange(ID3D11Resource *res, UINT mip, UINT slice)
{
#if ENABLED(RDOC_DEVEL)
  RDCASSERT(!res || WrappedID3D11Texture1D::IsAlloc(res) || WrappedID3D11Texture2D1::IsAlloc(res) ||
            WrappedID3D11Texture3D1::IsAlloc(res) || WrappedID3D11Buffer::IsAlloc(res));
#endif

  resource = res;
  minMip = mip;
  maxMip = mip;
  minSlice = slice;
  maxSlice = slice;
  fullRange = false;
  depthReadOnly = false;
  stencilReadOnly = false;
}

TextureDim MakeTextureDim(D3D11_SRV_DIMENSION dim)
{
  switch(dim)
  {
    case D3D11_SRV_DIMENSION_UNKNOWN: return TextureDim::Unknown;
    case D3D11_SRV_DIMENSION_BUFFER:
    case D3D11_SRV_DIMENSION_BUFFEREX: return TextureDim::Buffer;
    case D3D11_SRV_DIMENSION_TEXTURE1D: return TextureDim::Texture1D;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: return TextureDim::Texture1DArray;
    case D3D11_SRV_DIMENSION_TEXTURE2D: return TextureDim::Texture2D;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY: return TextureDim::Texture2DArray;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS: return TextureDim::Texture2DMS;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY: return TextureDim::Texture2DMSArray;
    case D3D11_SRV_DIMENSION_TEXTURE3D: return TextureDim::Texture3D;
    case D3D11_SRV_DIMENSION_TEXTURECUBE: return TextureDim::TextureCube;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY: return TextureDim::TextureCubeArray;
  }

  return TextureDim::Unknown;
}

TextureDim MakeTextureDim(D3D11_RTV_DIMENSION dim)
{
  switch(dim)
  {
    case D3D11_RTV_DIMENSION_UNKNOWN: return TextureDim::Unknown;
    case D3D11_RTV_DIMENSION_BUFFER: return TextureDim::Buffer;
    case D3D11_RTV_DIMENSION_TEXTURE1D: return TextureDim::Texture1D;
    case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: return TextureDim::Texture1DArray;
    case D3D11_RTV_DIMENSION_TEXTURE2D: return TextureDim::Texture2D;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: return TextureDim::Texture2DArray;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS: return TextureDim::Texture2DMS;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY: return TextureDim::Texture2DMSArray;
    case D3D11_RTV_DIMENSION_TEXTURE3D: return TextureDim::Texture3D;
  }

  return TextureDim::Unknown;
}

TextureDim MakeTextureDim(D3D11_DSV_DIMENSION dim)
{
  switch(dim)
  {
    case D3D11_DSV_DIMENSION_UNKNOWN: return TextureDim::Unknown;
    case D3D11_DSV_DIMENSION_TEXTURE1D: return TextureDim::Texture1D;
    case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: return TextureDim::Texture1DArray;
    case D3D11_DSV_DIMENSION_TEXTURE2D: return TextureDim::Texture2D;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: return TextureDim::Texture2DArray;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS: return TextureDim::Texture2DMS;
    case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY: return TextureDim::Texture2DMSArray;
  }

  return TextureDim::Unknown;
}

TextureDim MakeTextureDim(D3D11_UAV_DIMENSION dim)
{
  switch(dim)
  {
    case D3D11_UAV_DIMENSION_UNKNOWN: return TextureDim::Unknown;
    case D3D11_UAV_DIMENSION_BUFFER: return TextureDim::Buffer;
    case D3D11_UAV_DIMENSION_TEXTURE1D: return TextureDim::Texture1D;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY: return TextureDim::Texture1DArray;
    case D3D11_UAV_DIMENSION_TEXTURE2D: return TextureDim::Texture2D;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: return TextureDim::Texture2DArray;
    case D3D11_UAV_DIMENSION_TEXTURE3D: return TextureDim::Texture3D;
  }

  return TextureDim::Unknown;
}

AddressMode MakeAddressMode(D3D11_TEXTURE_ADDRESS_MODE addr)
{
  switch(addr)
  {
    case D3D11_TEXTURE_ADDRESS_WRAP: return AddressMode::Wrap;
    case D3D11_TEXTURE_ADDRESS_MIRROR: return AddressMode::Mirror;
    case D3D11_TEXTURE_ADDRESS_CLAMP: return AddressMode::ClampEdge;
    case D3D11_TEXTURE_ADDRESS_BORDER: return AddressMode::ClampBorder;
    case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return AddressMode::MirrorOnce;
    default: break;
  }

  return AddressMode::Wrap;
}

CompareFunc MakeCompareFunc(D3D11_COMPARISON_FUNC func)
{
  switch(func)
  {
    case D3D11_COMPARISON_NEVER: return CompareFunc::Never;
    case D3D11_COMPARISON_LESS: return CompareFunc::Less;
    case D3D11_COMPARISON_EQUAL: return CompareFunc::Equal;
    case D3D11_COMPARISON_LESS_EQUAL: return CompareFunc::LessEqual;
    case D3D11_COMPARISON_GREATER: return CompareFunc::Greater;
    case D3D11_COMPARISON_NOT_EQUAL: return CompareFunc::NotEqual;
    case D3D11_COMPARISON_GREATER_EQUAL: return CompareFunc::GreaterEqual;
    case D3D11_COMPARISON_ALWAYS: return CompareFunc::AlwaysTrue;
    default: break;
  }

  return CompareFunc::AlwaysTrue;
}

TextureFilter MakeFilter(D3D11_FILTER filter)
{
  TextureFilter ret;

  ret.func = FilterFunc::Normal;

  if(filter >= D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT &&
     filter < D3D11_FILTER_COMPARISON_ANISOTROPIC)
  {
    ret.func = FilterFunc::Comparison;
    // the first 0x7f is the min/mag/mip filtering
    filter = D3D11_FILTER(filter & 0x7f);
  }
  else if(filter >= D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT &&
          filter < D3D11_FILTER_MINIMUM_ANISOTROPIC)
  {
    ret.func = FilterFunc::Minimum;
    // the first 0x7f is the min/mag/mip filtering
    filter = D3D11_FILTER(filter & 0x7f);
  }
  else if(filter >= D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT &&
          filter < D3D11_FILTER_MAXIMUM_ANISOTROPIC)
  {
    ret.func = FilterFunc::Maximum;
    // the first 0x7f is the min/mag/mip filtering
    filter = D3D11_FILTER(filter & 0x7f);
  }

  if(filter == D3D11_FILTER_ANISOTROPIC)
  {
    ret.minify = ret.magnify = ret.mip = FilterMode::Anisotropic;
  }
  else
  {
    switch(filter)
    {
      case D3D11_FILTER_MIN_MAG_MIP_POINT:
        ret.minify = ret.magnify = ret.mip = FilterMode::Point;
        break;
      case D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:
        ret.minify = ret.magnify = FilterMode::Point;
        ret.mip = FilterMode::Linear;
        break;
      case D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:
        ret.minify = FilterMode::Point;
        ret.magnify = FilterMode::Linear;
        ret.mip = FilterMode::Point;
        break;
      case D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR:
        ret.minify = FilterMode::Point;
        ret.magnify = ret.mip = FilterMode::Linear;
        break;
      case D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT:
        ret.minify = FilterMode::Linear;
        ret.magnify = ret.mip = FilterMode::Point;
        break;
      case D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
        ret.minify = FilterMode::Linear;
        ret.magnify = FilterMode::Point;
        ret.mip = FilterMode::Linear;
        break;
      case D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:
        ret.minify = ret.magnify = FilterMode::Linear;
        ret.mip = FilterMode::Point;
        break;
      case D3D11_FILTER_MIN_MAG_MIP_LINEAR:
        ret.minify = ret.magnify = ret.mip = FilterMode::Linear;
        break;
    }
  }

  return ret;
}

LogicOp MakeLogicOp(D3D11_LOGIC_OP op)
{
  switch(op)
  {
    case D3D11_LOGIC_OP_CLEAR: return LogicOp::Clear;
    case D3D11_LOGIC_OP_AND: return LogicOp::And;
    case D3D11_LOGIC_OP_AND_REVERSE: return LogicOp::AndReverse;
    case D3D11_LOGIC_OP_COPY: return LogicOp::Copy;
    case D3D11_LOGIC_OP_AND_INVERTED: return LogicOp::AndInverted;
    case D3D11_LOGIC_OP_NOOP: return LogicOp::NoOp;
    case D3D11_LOGIC_OP_XOR: return LogicOp::Xor;
    case D3D11_LOGIC_OP_OR: return LogicOp::Or;
    case D3D11_LOGIC_OP_NOR: return LogicOp::Nor;
    case D3D11_LOGIC_OP_EQUIV: return LogicOp::Equivalent;
    case D3D11_LOGIC_OP_INVERT: return LogicOp::Invert;
    case D3D11_LOGIC_OP_OR_REVERSE: return LogicOp::OrReverse;
    case D3D11_LOGIC_OP_COPY_INVERTED: return LogicOp::CopyInverted;
    case D3D11_LOGIC_OP_OR_INVERTED: return LogicOp::OrInverted;
    case D3D11_LOGIC_OP_NAND: return LogicOp::Nand;
    case D3D11_LOGIC_OP_SET: return LogicOp::Set;
    default: break;
  }

  return LogicOp::NoOp;
}

BlendMultiplier MakeBlendMultiplier(D3D11_BLEND blend, bool alpha)
{
  switch(blend)
  {
    case D3D11_BLEND_ZERO: return BlendMultiplier::Zero;
    case D3D11_BLEND_ONE: return BlendMultiplier::One;
    case D3D11_BLEND_SRC_COLOR: return BlendMultiplier::SrcCol;
    case D3D11_BLEND_INV_SRC_COLOR: return BlendMultiplier::InvSrcCol;
    case D3D11_BLEND_DEST_COLOR: return BlendMultiplier::DstCol;
    case D3D11_BLEND_INV_DEST_COLOR: return BlendMultiplier::InvDstCol;
    case D3D11_BLEND_SRC_ALPHA: return BlendMultiplier::SrcAlpha;
    case D3D11_BLEND_INV_SRC_ALPHA: return BlendMultiplier::InvSrcAlpha;
    case D3D11_BLEND_DEST_ALPHA: return BlendMultiplier::DstAlpha;
    case D3D11_BLEND_INV_DEST_ALPHA: return BlendMultiplier::InvDstAlpha;
    case D3D11_BLEND_BLEND_FACTOR:
      return alpha ? BlendMultiplier::FactorAlpha : BlendMultiplier::FactorRGB;
    case D3D11_BLEND_INV_BLEND_FACTOR:
      return alpha ? BlendMultiplier::InvFactorAlpha : BlendMultiplier::InvFactorRGB;
    case D3D11_BLEND_SRC_ALPHA_SAT: return BlendMultiplier::SrcAlphaSat;
    case D3D11_BLEND_SRC1_COLOR: return BlendMultiplier::Src1Col;
    case D3D11_BLEND_INV_SRC1_COLOR: return BlendMultiplier::InvSrc1Col;
    case D3D11_BLEND_SRC1_ALPHA: return BlendMultiplier::Src1Alpha;
    case D3D11_BLEND_INV_SRC1_ALPHA: return BlendMultiplier::InvSrc1Alpha;
    default: break;
  }

  return BlendMultiplier::One;
}

BlendOp MakeBlendOp(D3D11_BLEND_OP op)
{
  switch(op)
  {
    case D3D11_BLEND_OP_ADD: return BlendOp::Add;
    case D3D11_BLEND_OP_SUBTRACT: return BlendOp::Subtract;
    case D3D11_BLEND_OP_REV_SUBTRACT: return BlendOp::ReversedSubtract;
    case D3D11_BLEND_OP_MIN: return BlendOp::Minimum;
    case D3D11_BLEND_OP_MAX: return BlendOp::Maximum;
    default: break;
  }

  return BlendOp::Add;
}

StencilOp MakeStencilOp(D3D11_STENCIL_OP op)
{
  switch(op)
  {
    case D3D11_STENCIL_OP_KEEP: return StencilOp::Keep;
    case D3D11_STENCIL_OP_ZERO: return StencilOp::Zero;
    case D3D11_STENCIL_OP_REPLACE: return StencilOp::Replace;
    case D3D11_STENCIL_OP_INCR_SAT: return StencilOp::IncSat;
    case D3D11_STENCIL_OP_DECR_SAT: return StencilOp::DecSat;
    case D3D11_STENCIL_OP_INVERT: return StencilOp::Invert;
    case D3D11_STENCIL_OP_INCR: return StencilOp::IncWrap;
    case D3D11_STENCIL_OP_DECR: return StencilOp::DecWrap;
    default: break;
  }

  return StencilOp::Keep;
}

/////////////////////////////////////////////////////////////
// Structures/descriptors. Serialise members separately
// instead of ToStrInternal separately. Mostly for convenience of
// debugging the output

template <>
void Serialiser::Serialise(const char *name, D3D11_BUFFER_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_BUFFER_DESC", 0, true);
  Serialise("ByteWidth", el.ByteWidth);
  Serialise("Usage", el.Usage);
  Serialise("BindFlags", (D3D11_BIND_FLAG &)el.BindFlags);
  Serialise("CPUAccessFlags", (D3D11_CPU_ACCESS_FLAG &)el.CPUAccessFlags);
  Serialise("MiscFlags", (D3D11_RESOURCE_MISC_FLAG &)el.MiscFlags);
  Serialise("StructureByteStride", el.StructureByteStride);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_TEXTURE1D_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_TEXTURE1D_DESC", 0, true);
  Serialise("Width", el.Width);
  Serialise("MipLevels", el.MipLevels);
  Serialise("ArraySize", el.ArraySize);
  Serialise("Format", el.Format);
  Serialise("Usage", el.Usage);
  Serialise("BindFlags", (D3D11_BIND_FLAG &)el.BindFlags);
  Serialise("CPUAccessFlags", (D3D11_CPU_ACCESS_FLAG &)el.CPUAccessFlags);
  Serialise("MiscFlags", (D3D11_RESOURCE_MISC_FLAG &)el.MiscFlags);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_TEXTURE2D_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_TEXTURE2D_DESC", 0, true);
  Serialise("Width", el.Width);
  Serialise("Height", el.Height);
  Serialise("MipLevels", el.MipLevels);
  Serialise("ArraySize", el.ArraySize);
  Serialise("Format", el.Format);
  Serialise("SampleDesc", el.SampleDesc);
  Serialise("Usage", el.Usage);
  Serialise("BindFlags", (D3D11_BIND_FLAG &)el.BindFlags);
  Serialise("CPUAccessFlags", (D3D11_CPU_ACCESS_FLAG &)el.CPUAccessFlags);
  Serialise("MiscFlags", (D3D11_RESOURCE_MISC_FLAG &)el.MiscFlags);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_TEXTURE2D_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_TEXTURE2D_DESC1", 0, true);
  Serialise("Width", el.Width);
  Serialise("Height", el.Height);
  Serialise("MipLevels", el.MipLevels);
  Serialise("ArraySize", el.ArraySize);
  Serialise("Format", el.Format);
  Serialise("SampleDesc", el.SampleDesc);
  Serialise("Usage", el.Usage);
  Serialise("BindFlags", (D3D11_BIND_FLAG &)el.BindFlags);
  Serialise("CPUAccessFlags", (D3D11_CPU_ACCESS_FLAG &)el.CPUAccessFlags);
  Serialise("MiscFlags", (D3D11_RESOURCE_MISC_FLAG &)el.MiscFlags);
  Serialise("TextureLayout", (D3D11_TEXTURE_LAYOUT &)el.TextureLayout);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_TEXTURE3D_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_TEXTURE3D_DESC", 0, true);
  Serialise("Width", el.Width);
  Serialise("Height", el.Height);
  Serialise("Depth", el.Depth);
  Serialise("MipLevels", el.MipLevels);
  Serialise("Format", el.Format);
  Serialise("Usage", el.Usage);
  Serialise("BindFlags", (D3D11_BIND_FLAG &)el.BindFlags);
  Serialise("CPUAccessFlags", (D3D11_CPU_ACCESS_FLAG &)el.CPUAccessFlags);
  Serialise("MiscFlags", (D3D11_RESOURCE_MISC_FLAG &)el.MiscFlags);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_TEXTURE3D_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_TEXTURE3D_DESC1", 0, true);
  Serialise("Width", el.Width);
  Serialise("Height", el.Height);
  Serialise("Depth", el.Depth);
  Serialise("MipLevels", el.MipLevels);
  Serialise("Format", el.Format);
  Serialise("Usage", el.Usage);
  Serialise("BindFlags", (D3D11_BIND_FLAG &)el.BindFlags);
  Serialise("CPUAccessFlags", (D3D11_CPU_ACCESS_FLAG &)el.CPUAccessFlags);
  Serialise("MiscFlags", (D3D11_RESOURCE_MISC_FLAG &)el.MiscFlags);
  Serialise("TextureLayout", (D3D11_TEXTURE_LAYOUT &)el.TextureLayout);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_SHADER_RESOURCE_VIEW_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_SHADER_RESOURCE_VIEW_DESC", 0, true);
  Serialise("Format", el.Format);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_SRV_DIMENSION_BUFFER:
      Serialise("Buffer.FirstElement", el.Buffer.FirstElement);
      Serialise("Buffer.NumElements", el.Buffer.NumElements);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipLevels", el.Texture1D.MipLevels);
      Serialise("Texture1D.MostDetailedMip", el.Texture1D.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipLevels", el.Texture1DArray.MipLevels);
      Serialise("Texture1DArray.MostDetailedMip", el.Texture1DArray.MostDetailedMip);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipLevels", el.Texture2D.MipLevels);
      Serialise("Texture2D.MostDetailedMip", el.Texture2D.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipLevels", el.Texture2DArray.MipLevels);
      Serialise("Texture2DArray.MostDetailedMip", el.Texture2DArray.MostDetailedMip);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS:
      // el.Texture2DMS.UnusedField_NothingToDefine
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
      Serialise("Texture2DMSArray.ArraySize", el.Texture2DMSArray.ArraySize);
      Serialise("Texture2DMSArray.FirstArraySlice", el.Texture2DMSArray.FirstArraySlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE3D:
      Serialise("Texture3D.MipLevels", el.Texture3D.MipLevels);
      Serialise("Texture3D.MostDetailedMip", el.Texture3D.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
      Serialise("TextureCube.MipLevels", el.TextureCube.MipLevels);
      Serialise("TextureCube.MostDetailedMip", el.TextureCube.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
      Serialise("TextureCubeArray.MipLevels", el.TextureCubeArray.MipLevels);
      Serialise("TextureCubeArray.MostDetailedMip", el.TextureCubeArray.MostDetailedMip);
      Serialise("TextureCubeArray.NumCubes", el.TextureCubeArray.NumCubes);
      Serialise("TextureCubeArray.First2DArrayFace", el.TextureCubeArray.First2DArrayFace);
      break;
    case D3D11_SRV_DIMENSION_BUFFEREX:
      Serialise("Buffer.FirstElement", el.BufferEx.FirstElement);
      Serialise("Buffer.NumElements", el.BufferEx.NumElements);
      Serialise("Buffer.Flags", el.BufferEx.Flags);
      break;
    default: RDCERR("Unrecognised SRV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_SHADER_RESOURCE_VIEW_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_SHADER_RESOURCE_VIEW_DESC1", 0, true);
  Serialise("Format", el.Format);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_SRV_DIMENSION_BUFFER:
      Serialise("Buffer.FirstElement", el.Buffer.FirstElement);
      Serialise("Buffer.NumElements", el.Buffer.NumElements);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipLevels", el.Texture1D.MipLevels);
      Serialise("Texture1D.MostDetailedMip", el.Texture1D.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipLevels", el.Texture1DArray.MipLevels);
      Serialise("Texture1DArray.MostDetailedMip", el.Texture1DArray.MostDetailedMip);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipLevels", el.Texture2D.MipLevels);
      Serialise("Texture2D.MostDetailedMip", el.Texture2D.MostDetailedMip);
      Serialise("Texture2D.PlaneSlice", el.Texture2D.PlaneSlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipLevels", el.Texture2DArray.MipLevels);
      Serialise("Texture2DArray.MostDetailedMip", el.Texture2DArray.MostDetailedMip);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      Serialise("Texture2DArray.PlaneSlice", el.Texture2DArray.PlaneSlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS:
      // el.Texture2DMS.UnusedField_NothingToDefine
      break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
      Serialise("Texture2DMSArray.ArraySize", el.Texture2DMSArray.ArraySize);
      Serialise("Texture2DMSArray.FirstArraySlice", el.Texture2DMSArray.FirstArraySlice);
      break;
    case D3D11_SRV_DIMENSION_TEXTURE3D:
      Serialise("Texture3D.MipLevels", el.Texture3D.MipLevels);
      Serialise("Texture3D.MostDetailedMip", el.Texture3D.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
      Serialise("TextureCube.MipLevels", el.TextureCube.MipLevels);
      Serialise("TextureCube.MostDetailedMip", el.TextureCube.MostDetailedMip);
      break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
      Serialise("TextureCubeArray.MipLevels", el.TextureCubeArray.MipLevels);
      Serialise("TextureCubeArray.MostDetailedMip", el.TextureCubeArray.MostDetailedMip);
      Serialise("TextureCubeArray.NumCubes", el.TextureCubeArray.NumCubes);
      Serialise("TextureCubeArray.First2DArrayFace", el.TextureCubeArray.First2DArrayFace);
      break;
    case D3D11_SRV_DIMENSION_BUFFEREX:
      Serialise("Buffer.FirstElement", el.BufferEx.FirstElement);
      Serialise("Buffer.NumElements", el.BufferEx.NumElements);
      Serialise("Buffer.Flags", el.BufferEx.Flags);
      break;
    default: RDCERR("Unrecognised SRV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_RENDER_TARGET_VIEW_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_RENDER_TARGET_VIEW_DESC", 0, true);
  Serialise("Format", el.Format);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_RTV_DIMENSION_BUFFER:
      Serialise("Buffer.FirstElement", el.Buffer.FirstElement);
      Serialise("Buffer.NumElements", el.Buffer.NumElements);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipSlice", el.Texture1D.MipSlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipSlice", el.Texture1DArray.MipSlice);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipSlice", el.Texture2D.MipSlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipSlice", el.Texture2DArray.MipSlice);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS:
      // el.Texture2DMS.UnusedField_NothingToDefine
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
      Serialise("Texture2DMSArray.ArraySize", el.Texture2DMSArray.ArraySize);
      Serialise("Texture2DMSArray.FirstArraySlice", el.Texture2DMSArray.FirstArraySlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE3D:
      Serialise("Texture3D.MipSlice", el.Texture3D.MipSlice);
      Serialise("Texture3D.FirstWSlice", el.Texture3D.FirstWSlice);
      Serialise("Texture3D.WSize", el.Texture3D.WSize);
      break;
    default: RDCERR("Unrecognised RTV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_RENDER_TARGET_VIEW_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_RENDER_TARGET_VIEW_DESC1", 0, true);
  Serialise("Format", el.Format);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_RTV_DIMENSION_BUFFER:
      Serialise("Buffer.FirstElement", el.Buffer.FirstElement);
      Serialise("Buffer.NumElements", el.Buffer.NumElements);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipSlice", el.Texture1D.MipSlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipSlice", el.Texture1DArray.MipSlice);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipSlice", el.Texture2D.MipSlice);
      Serialise("Texture2D.PlaneSlice", el.Texture2D.PlaneSlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipSlice", el.Texture2DArray.MipSlice);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      Serialise("Texture2DArray.PlaneSlice", el.Texture2DArray.PlaneSlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS:
      // el.Texture2DMS.UnusedField_NothingToDefine
      break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
      Serialise("Texture2DMSArray.ArraySize", el.Texture2DMSArray.ArraySize);
      Serialise("Texture2DMSArray.FirstArraySlice", el.Texture2DMSArray.FirstArraySlice);
      break;
    case D3D11_RTV_DIMENSION_TEXTURE3D:
      Serialise("Texture3D.MipSlice", el.Texture3D.MipSlice);
      Serialise("Texture3D.FirstWSlice", el.Texture3D.FirstWSlice);
      Serialise("Texture3D.WSize", el.Texture3D.WSize);
      break;
    default: RDCERR("Unrecognised RTV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_UNORDERED_ACCESS_VIEW_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_UNORDERED_ACCESS_VIEW_DESC", 0, true);
  Serialise("Format", el.Format);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_UAV_DIMENSION_BUFFER:
      Serialise("Buffer.FirstElement", el.Buffer.FirstElement);
      Serialise("Buffer.NumElements", el.Buffer.NumElements);
      Serialise("Buffer.Flags", el.Buffer.Flags);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipSlice", el.Texture1D.MipSlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipSlice", el.Texture1DArray.MipSlice);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipSlice", el.Texture2D.MipSlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipSlice", el.Texture2DArray.MipSlice);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE3D:
      Serialise("Texture3D.MipSlice", el.Texture3D.MipSlice);
      Serialise("Texture3D.FirstWSlice", el.Texture3D.FirstWSlice);
      Serialise("Texture3D.WSize", el.Texture3D.WSize);
      break;
    default: RDCERR("Unrecognised RTV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_UNORDERED_ACCESS_VIEW_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_UNORDERED_ACCESS_VIEW_DESC1", 0, true);
  Serialise("Format", el.Format);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_UAV_DIMENSION_BUFFER:
      Serialise("Buffer.FirstElement", el.Buffer.FirstElement);
      Serialise("Buffer.NumElements", el.Buffer.NumElements);
      Serialise("Buffer.Flags", el.Buffer.Flags);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipSlice", el.Texture1D.MipSlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipSlice", el.Texture1DArray.MipSlice);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipSlice", el.Texture2D.MipSlice);
      Serialise("Texture2D.PlaneSlice", el.Texture2D.PlaneSlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipSlice", el.Texture2DArray.MipSlice);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      Serialise("Texture2DArray.PlaneSlice", el.Texture2DArray.PlaneSlice);
      break;
    case D3D11_UAV_DIMENSION_TEXTURE3D:
      Serialise("Texture3D.MipSlice", el.Texture3D.MipSlice);
      Serialise("Texture3D.FirstWSlice", el.Texture3D.FirstWSlice);
      Serialise("Texture3D.WSize", el.Texture3D.WSize);
      break;
    default: RDCERR("Unrecognised RTV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_DEPTH_STENCIL_VIEW_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_DEPTH_STENCIL_VIEW_DESC", 0, true);
  Serialise("Format", el.Format);
  Serialise("Flags", el.Flags);
  Serialise("ViewDimension", el.ViewDimension);

  switch(el.ViewDimension)
  {
    case D3D11_DSV_DIMENSION_TEXTURE1D:
      Serialise("Texture1D.MipSlice", el.Texture1D.MipSlice);
      break;
    case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
      Serialise("Texture1DArray.MipSlice", el.Texture1DArray.MipSlice);
      Serialise("Texture1DArray.ArraySize", el.Texture1DArray.ArraySize);
      Serialise("Texture1DArray.FirstArraySlice", el.Texture1DArray.FirstArraySlice);
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2D:
      Serialise("Texture2D.MipSlice", el.Texture2D.MipSlice);
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
      Serialise("Texture2DArray.MipSlice", el.Texture2DArray.MipSlice);
      Serialise("Texture2DArray.ArraySize", el.Texture2DArray.ArraySize);
      Serialise("Texture2DArray.FirstArraySlice", el.Texture2DArray.FirstArraySlice);
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS:
      // el.Texture2DMS.UnusedField_NothingToDefine
      break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
      Serialise("Texture2DMSArray.ArraySize", el.Texture2DMSArray.ArraySize);
      Serialise("Texture2DMSArray.FirstArraySlice", el.Texture2DMSArray.FirstArraySlice);
      break;
    default: RDCERR("Unrecognised DSV Dimension %d", el.ViewDimension); break;
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_BLEND_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_BLEND_DESC", 0, true);

  Serialise("AlphaToCoverageEnable", el.AlphaToCoverageEnable);
  Serialise("IndependentBlendEnable", el.IndependentBlendEnable);
  for(int i = 0; i < 8; i++)
  {
    ScopedContext targetscope(this, name, "D3D11_RENDER_TARGET_BLEND_DESC", 0, true);

    bool enable = el.RenderTarget[i].BlendEnable == TRUE;
    Serialise("BlendEnable", enable);
    el.RenderTarget[i].BlendEnable = enable;

    {
      Serialise("SrcBlend", el.RenderTarget[i].SrcBlend);
      Serialise("DestBlend", el.RenderTarget[i].DestBlend);
      Serialise("BlendOp", el.RenderTarget[i].BlendOp);
      Serialise("SrcBlendAlpha", el.RenderTarget[i].SrcBlendAlpha);
      Serialise("DestBlendAlpha", el.RenderTarget[i].DestBlendAlpha);
      Serialise("BlendOpAlpha", el.RenderTarget[i].BlendOpAlpha);
    }

    Serialise("RenderTargetWriteMask", el.RenderTarget[i].RenderTargetWriteMask);
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_BLEND_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_BLEND_DESC1", 0, true);

  Serialise("AlphaToCoverageEnable", el.AlphaToCoverageEnable);
  Serialise("IndependentBlendEnable", el.IndependentBlendEnable);
  for(int i = 0; i < 8; i++)
  {
    ScopedContext targetscope(this, name, "D3D11_RENDER_TARGET_BLEND_DESC1", 0, true);

    bool enable = el.RenderTarget[i].BlendEnable == TRUE;
    Serialise("BlendEnable", enable);
    el.RenderTarget[i].BlendEnable = enable;

    enable = el.RenderTarget[i].LogicOpEnable == TRUE;
    Serialise("LogicOpEnable", enable);
    el.RenderTarget[i].LogicOpEnable = enable;

    {
      Serialise("SrcBlend", el.RenderTarget[i].SrcBlend);
      Serialise("DestBlend", el.RenderTarget[i].DestBlend);
      Serialise("BlendOp", el.RenderTarget[i].BlendOp);
      Serialise("SrcBlendAlpha", el.RenderTarget[i].SrcBlendAlpha);
      Serialise("DestBlendAlpha", el.RenderTarget[i].DestBlendAlpha);
      Serialise("BlendOpAlpha", el.RenderTarget[i].BlendOpAlpha);
      Serialise("LogicOp", el.RenderTarget[i].LogicOp);
    }

    Serialise("RenderTargetWriteMask", el.RenderTarget[i].RenderTargetWriteMask);
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_DEPTH_STENCIL_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_DEPTH_STENCIL_DESC", 0, true);

  Serialise("DepthEnable", el.DepthEnable);
  Serialise("DepthWriteMask", el.DepthWriteMask);
  Serialise("DepthFunc", el.DepthFunc);
  Serialise("StencilEnable", el.StencilEnable);
  Serialise("StencilReadMask", el.StencilReadMask);
  Serialise("StencilWriteMask", el.StencilWriteMask);

  {
    ScopedContext opscope(this, name, "D3D11_DEPTH_STENCILOP_DESC", 0, true);
    Serialise("FrontFace.StencilFailOp", el.FrontFace.StencilFailOp);
    Serialise("FrontFace.StencilDepthFailOp", el.FrontFace.StencilDepthFailOp);
    Serialise("FrontFace.StencilPassOp", el.FrontFace.StencilPassOp);
    Serialise("FrontFace.StencilFunc", el.FrontFace.StencilFunc);
  }
  {
    ScopedContext opscope(this, name, "D3D11_DEPTH_STENCILOP_DESC", 0, true);
    Serialise("BackFace.StencilFailOp", el.BackFace.StencilFailOp);
    Serialise("BackFace.StencilDepthFailOp", el.BackFace.StencilDepthFailOp);
    Serialise("BackFace.StencilPassOp", el.BackFace.StencilPassOp);
    Serialise("BackFace.StencilFunc", el.BackFace.StencilFunc);
  }
}

template <>
void Serialiser::Serialise(const char *name, D3D11_RASTERIZER_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_RASTERIZER_DESC", 0, true);

  Serialise("FillMode", el.FillMode);
  Serialise("CullMode", el.CullMode);
  Serialise("FrontCounterClockwise", el.FrontCounterClockwise);
  Serialise("DepthBias", el.DepthBias);
  Serialise("DepthBiasClamp", el.DepthBiasClamp);
  Serialise("SlopeScaledDepthBias", el.SlopeScaledDepthBias);
  Serialise("DepthClipEnable", el.DepthClipEnable);
  Serialise("ScissorEnable", el.ScissorEnable);
  Serialise("MultisampleEnable", el.MultisampleEnable);
  Serialise("AntialiasedLineEnable", el.AntialiasedLineEnable);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_RASTERIZER_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_RASTERIZER_DESC1", 0, true);

  Serialise("FillMode", el.FillMode);
  Serialise("CullMode", el.CullMode);
  Serialise("FrontCounterClockwise", el.FrontCounterClockwise);
  Serialise("DepthBias", el.DepthBias);
  Serialise("DepthBiasClamp", el.DepthBiasClamp);
  Serialise("SlopeScaledDepthBias", el.SlopeScaledDepthBias);
  Serialise("DepthClipEnable", el.DepthClipEnable);
  Serialise("ScissorEnable", el.ScissorEnable);
  Serialise("MultisampleEnable", el.MultisampleEnable);
  Serialise("AntialiasedLineEnable", el.AntialiasedLineEnable);
  Serialise("ForcedSampleCount", el.ForcedSampleCount);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_RASTERIZER_DESC2 &el)
{
  ScopedContext scope(this, name, "D3D11_RASTERIZER_DESC2", 0, true);

  Serialise("FillMode", el.FillMode);
  Serialise("CullMode", el.CullMode);
  Serialise("FrontCounterClockwise", el.FrontCounterClockwise);
  Serialise("DepthBias", el.DepthBias);
  Serialise("DepthBiasClamp", el.DepthBiasClamp);
  Serialise("SlopeScaledDepthBias", el.SlopeScaledDepthBias);
  Serialise("DepthClipEnable", el.DepthClipEnable);
  Serialise("ScissorEnable", el.ScissorEnable);
  Serialise("MultisampleEnable", el.MultisampleEnable);
  Serialise("AntialiasedLineEnable", el.AntialiasedLineEnable);
  Serialise("ForcedSampleCount", el.ForcedSampleCount);
  Serialise("ConservativeRaster", (D3D11_CONSERVATIVE_RASTERIZATION_MODE &)el.ConservativeRaster);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_QUERY_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_QUERY_DESC", 0, true);

  Serialise("MiscFlags", el.MiscFlags);
  Serialise("Query", el.Query);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_QUERY_DESC1 &el)
{
  ScopedContext scope(this, name, "D3D11_QUERY_DESC1", 0, true);

  Serialise("MiscFlags", el.MiscFlags);
  Serialise("Query", el.Query);
  Serialise("ContextType", (D3D11_CONTEXT_TYPE &)el.ContextType);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_COUNTER_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_COUNTER_DESC", 0, true);

  Serialise("MiscFlags", el.MiscFlags);
  Serialise("Counter", el.Counter);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_SAMPLER_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_SAMPLER_DESC", 0, true);

  Serialise("Filter", el.Filter);
  Serialise("AddressU", el.AddressU);
  Serialise("AddressV", el.AddressV);
  Serialise("AddressW", el.AddressW);
  Serialise("MipLODBias", el.MipLODBias);
  Serialise("MaxAnisotropy", el.MaxAnisotropy);
  Serialise("ComparisonFunc", el.ComparisonFunc);
  SerialisePODArray<4>("BorderColor", el.BorderColor);
  Serialise("MinLOD", el.MinLOD);
  Serialise("MaxLOD", el.MaxLOD);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_SO_DECLARATION_ENTRY &el)
{
  ScopedContext scope(this, name, "D3D11_SO_DECLARATION_ENTRY", 0, true);

  string s = "";
  if(m_Mode >= WRITING && el.SemanticName != NULL)
    s = el.SemanticName;

  Serialise("SemanticName", s);

  if(m_Mode == READING)
  {
    if(s == "")
    {
      el.SemanticName = NULL;
    }
    else
    {
      string str = (char *)m_BufferHead - s.length();
      m_StringDB.insert(str);
      el.SemanticName = m_StringDB.find(str)->c_str();
    }
  }

  // so we can just take a char* into the buffer above for the semantic name,
  // ensure we serialise a null terminator (slightly redundant because the above
  // serialise of the string wrote out the length, but not the end of the world).
  char nullterminator = 0;
  Serialise(NULL, nullterminator);

  Serialise("SemanticIndex", el.SemanticIndex);
  Serialise("Stream", el.Stream);
  Serialise("StartComponent", el.StartComponent);
  Serialise("ComponentCount", el.ComponentCount);
  Serialise("OutputSlot", el.OutputSlot);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_INPUT_ELEMENT_DESC &el)
{
  ScopedContext scope(this, name, "D3D11_INPUT_ELEMENT_DESC", 0, true);

  string s;
  if(m_Mode >= WRITING)
    s = el.SemanticName;

  Serialise("SemanticName", s);

  if(m_Mode == READING)
  {
    string str = (char *)m_BufferHead - s.length();
    m_StringDB.insert(str);
    el.SemanticName = m_StringDB.find(str)->c_str();
  }

  // so we can just take a char* into the buffer above for the semantic name,
  // ensure we serialise a null terminator (slightly redundant because the above
  // serialise of the string wrote out the length, but not the end of the world).
  char nullterminator = 0;
  Serialise(NULL, nullterminator);

  Serialise("SemanticIndex", el.SemanticIndex);
  Serialise("Format", el.Format);
  Serialise("InputSlot", el.InputSlot);
  Serialise("AlignedByteOffset", el.AlignedByteOffset);
  Serialise("InputSlotClass", el.InputSlotClass);
  Serialise("InstanceDataStepRate", el.InstanceDataStepRate);
}

template <>
void Serialiser::Serialise(const char *name, D3D11_SUBRESOURCE_DATA &el)
{
  ScopedContext scope(this, name, "D3D11_SUBRESOURCE_DATA", 0, true);

  // el.pSysMem
  Serialise("SysMemPitch", el.SysMemPitch);
  Serialise("SysMemSlicePitch", el.SysMemSlicePitch);
}

template <>
std::string DoStringise(const D3D11_BOX &el)
{
  return StringFormat::Fmt("BOX<%d,%d,%d,%d,%d,%d>", el.left, el.right, el.top, el.bottom, el.front,
                           el.back);
}

template <>
std::string DoStringise(const D3D11_VIEWPORT &el)
{
  return StringFormat::Fmt("Viewport<%.0fx%.0f+%.0f+%.0f z=%f->%f>", el.Width, el.Height,
                           el.TopLeftX, el.TopLeftY, el.MinDepth, el.MaxDepth);
}