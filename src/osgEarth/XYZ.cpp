/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
 * Copyright 2020 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include "XYZ"
#include <osgEarth/Registry>
#include <osgEarth/FileUtils>
#include <osgEarth/XmlUtils>
#include <osgEarth/ImageToHeightFieldConverter>
#include "MetaTile"
#include <osgDB/FileUtils>

using namespace osgEarth;
using namespace osgEarth::XYZ;

#undef LC
#define LC "[XYZ] "

//............................................................................

Status
XYZ::Driver::open(const URI& uri,
                  const std::string& format,
                  DataExtentList& out_dataExtents,
                  const osgDB::Options* readOptions)
{
    if (uri.empty())
    {
        return Status::Error(Status::ConfigurationError, "Valid URL is missing");
    }

    _template = uri.full();

    // Set up a rotating element in the template
    _rotateStart = _template.find('[');
    _rotateEnd = _template.find(']');
    if (_rotateStart != std::string::npos && _rotateEnd != std::string::npos && _rotateEnd - _rotateStart > 1)
    {
        _rotateString = _template.substr(_rotateStart, _rotateEnd - _rotateStart + 1);
        _rotateChoices = _template.substr(_rotateStart + 1, _rotateEnd - _rotateStart - 1);
    }

    _format = !format.empty() ? format : osgDB::getLowerCaseFileExtension(uri.base());

    return STATUS_OK;
}

osgEarth::ReadResult
XYZ::Driver::read(const URI& uri,
                  const TileKey& key, 
                  bool invertY,
                  ProgressCallback* progress,
                  const osgDB::Options* readOptions) const
{
    unsigned x, y;
    key.getTileXY(x, y);
    unsigned cols = 0, rows = 0;
    key.getProfile()->getNumTiles(key.getLevelOfDetail(), cols, rows);
    unsigned inverted_y = rows - y - 1;

    if (invertY == true)
    {
        y = inverted_y;
    }

    std::string location = _template;

    // support OpenLayers template style:
    replaceIn( location, "${x}", Stringify() << x );
    replaceIn( location, "${y}", Stringify() << y );
    replaceIn( location, "${-y}", Stringify() << inverted_y);
    replaceIn( location, "${z}", Stringify() << key.getLevelOfDetail() );


    // failing that, legacy osgearth style:
    replaceIn( location, "{x}", Stringify() << x );
    replaceIn( location, "{y}", Stringify() << y );
    replaceIn( location, "{-y}", Stringify() << inverted_y);
    replaceIn( location, "{z}", Stringify() << key.getLevelOfDetail() );

    std::string cacheKey;

    if ( !_rotateChoices.empty() )
    {
        cacheKey = location;
        unsigned index = (++_rotate_iter) % _rotateChoices.size();
        replaceIn( location, _rotateString, Stringify() << _rotateChoices[index] );
    }


    URI myUri( location, uri.context() );
    if ( !cacheKey.empty() )
    {
        myUri.setCacheKey(Cache::makeCacheKey(location, "uri"));
    }

    return myUri.getImage(readOptions, progress);
}

//........................................................................

Config
XYZImageLayerOptions::getConfig() const
{
    Config conf = ImageLayer::Options::getConfig();
    conf.set("url", _url);
    conf.set("format", _format);
    conf.set("invert_y", _invertY);
    return conf;
}

void
XYZImageLayerOptions::fromConfig(const Config& conf)
{
    invertY().setDefault(false);

    conf.get("url", _url);
    conf.get("format", _format);
    conf.get("invert_y", _invertY);
}

Config
XYZImageLayerOptions::getMetadata()
{
    return Config::readJSON( R"(
        { "name" : "XYZ Image Tile Service",
            "properties": [
            { "name": "url",      "description": "Location of the TMS repository", "type": "string", "default": "" },
            { "name": "invert_y", "description": "Set to true invert the Y index", "type": "bool", "default": "false" },
            { "name": "format",   "description": "Image format to assume", "type": "string", "default": "" }
            ]
        }
    )" );
}

//........................................................................

Config
XYZElevationLayerOptions::getConfig() const
{
    Config conf = ElevationLayer::Options::getConfig();
    conf.set("url", _url);
    conf.set("format", _format);
    conf.set("invert_y", _invertY);
    conf.set("elevation_encoding", _elevationEncoding);
    conf.set("stitch_edges", stitchEdges());
    return conf;
}

void
XYZElevationLayerOptions::fromConfig(const Config& conf)
{
    conf.get("url", _url);
    conf.get("format", _format);
    conf.get("invert_y", _invertY);
    conf.get("elevation_encoding", _elevationEncoding);
    conf.get("interpretation", elevationEncoding()); // compat with QGIS
    conf.get("stitch_edges", stitchEdges());
}

Config
XYZElevationLayerOptions::getMetadata()
{
    return Config::readJSON( R"(
        { "name" : "XYZ Elevation Tile Service",
            "properties": [
            { "name": "url",      "description": "Location of the TMS repository", "type": "string", "default": "" },
            { "name": "invert_y", "description": "Set to true invert the Y index", "type": "bool", "default": "false" },
            { "name": "format",   "description": "Image format to assume", "type": "string", "default": "" },
            { "name": "elevation_encoding", "description": "How elevation is encoded", "type": "string", "default": "" }
            ]
        }
    )" );
}

//........................................................................

REGISTER_OSGEARTH_LAYER(xyzimage, XYZImageLayer);

OE_LAYER_PROPERTY_IMPL(XYZImageLayer, URI, URL, url);
OE_LAYER_PROPERTY_IMPL(XYZImageLayer, bool, InvertY, invertY);
OE_LAYER_PROPERTY_IMPL(XYZImageLayer, std::string, Format, format);

void
XYZImageLayer::init()
{
    ImageLayer::init();
}

void
XYZImageLayer::setProfile(const Profile* profile)
{
    ImageLayer::setProfile(profile);

    if (profile)
    {
        // update the options for proper serialization
        options().profile() = profile->toProfileOptions();
    }
}

Status
XYZImageLayer::openImplementation()
{
    Status parent = ImageLayer::openImplementation();
    if (parent.isError())
        return parent;

    DataExtentList dataExtents;

    Status status = _driver.open(
        options().url().get(),
        options().format().get(),
        dataExtents,
        getReadOptions());

    if (status.isError())
        return status;

    if (!getProfile())
    {
        OE_INFO << LC << "No profile; assuming spherical-mercator" << std::endl;
        setProfile(Profile::create("spherical-mercator"));
    }

    if (dataExtents.empty())
    {
        dataExtents.push_back(DataExtent(getProfile()->getExtent()));
    }

    setDataExtents(dataExtents);

    return Status::NoError;
}

GeoImage
XYZImageLayer::createImageImplementation(const TileKey& key, ProgressCallback* progress) const
{
    ReadResult r = _driver.read(
        options().url().get(),
        key,
        options().invertY() == true,
        progress,
        getReadOptions());

    if (r.succeeded())
        return GeoImage(r.releaseImage(), key.getExtent());
    else
        return GeoImage(Status(r.errorDetail()));
}

//........................................................................

REGISTER_OSGEARTH_LAYER(xyzelevation, XYZElevationLayer);

OE_LAYER_PROPERTY_IMPL(XYZElevationLayer, URI, URL, url);
OE_LAYER_PROPERTY_IMPL(XYZElevationLayer, bool, InvertY, invertY);
OE_LAYER_PROPERTY_IMPL(XYZElevationLayer, std::string, Format, format);
OE_LAYER_PROPERTY_IMPL(XYZElevationLayer, std::string, ElevationEncoding, elevationEncoding);
OE_LAYER_PROPERTY_IMPL(XYZElevationLayer, bool, StitchEdges, stitchEdges);
OE_LAYER_PROPERTY_IMPL(XYZElevationLayer, RasterInterpolation, Interpolation, interpolation);

void
XYZElevationLayer::init()
{
    ElevationLayer::init();
}

void
XYZElevationLayer::setProfile(const Profile* profile)
{
    // do not use profile after callin this function
    ElevationLayer::setProfile(profile);

    if (getProfile())
    {
        // update the options for proper serialization
        options().profile() = getProfile()->toProfileOptions();
    }
}

Status
XYZElevationLayer::openImplementation()
{
    Status parent = ElevationLayer::openImplementation();
    if (parent.isError())
        return parent;

    // Create an image layer under the hood. TMS fetch is the same for image and
    // elevation; we just convert the resulting image to a heightfield
    _imageLayer = new XYZImageLayer(options());

    // Initialize and open the image layer
    _imageLayer->setReadOptions(getReadOptions());
    Status status = _imageLayer->open();

    if (status.isError())
        return status;

    setProfile(_imageLayer->getProfile());            

    return Status::NoError;
}

GeoHeightField
XYZElevationLayer::createHeightFieldImplementation(const TileKey& key, ProgressCallback* progress) const
{
    if (!_imageLayer->mayHaveData(key))
    {
        return GeoHeightField::INVALID;
    }

    std::function<float(const osg::Vec4f&)> decode;
    
    if (options().elevationEncoding() == "mapbox")
    {
        decode = [](const osg::Vec4f& p) -> float
            {
                return -10000.0f + ((p.r() * 255.0f * 256.0f * 256.0f + p.g() * 255.0f * 256.0f + p.b() * 255.0f) * 0.1f);
            };
    }
    else if (options().elevationEncoding() == "terrarium")
    {
        decode = [](const osg::Vec4f& p) -> float
            {
                return (p.r() * 255.0f * 256.0f + p.g() * 255.0f + p.b() * 255.0f / 256.0f) - 32768.0f;
            };
    }
    else
    {
        decode = [](const osg::Vec4f& p) -> float
            {
                return p.r();
            };
    }

    if (options().stitchEdges() == true)
    {
        MetaTile<GeoImage> metaImage;
        metaImage.setCreateTileFunction([this](const TileKey& key, ProgressCallback* progress)
            {
                Util::LRUCache<TileKey, GeoImage>::Record r;
                if (_stitchingCache.get(key, r))
                {
                    return r.value();
                }
                else
                {
                    GeoImage image = _imageLayer->createImage(key, progress);
                    if (image.valid())
                    {
                        _stitchingCache.insert(key, image);
                    }
                    return image;
                }
            });
        metaImage.setCenterTileKey(key, progress);

        if (!metaImage.getCenterTile().valid() || !metaImage.getScaleBias().isIdentity())
        {
            return {};
        }

        osg::ref_ptr<osg::HeightField> hf = new osg::HeightField();
        hf->allocate(getTileSize(), getTileSize());
        std::fill(hf->getHeightList().begin(), hf->getHeightList().end(), NO_DATA_VALUE);

        double xmin, ymin, xmax, ymax;
        key.getExtent().getBounds(xmin, ymin, xmax, ymax);

        osg::Vec4f pixel;
        for (int c = 0; c < getTileSize(); c++)
        {
            if (progress && progress->isCanceled())
                return {};

            double u = (double)c / (double)(getTileSize() - 1);
            double x = xmin + u * (xmax - xmin);
            for (int r = 0; r < getTileSize(); r++)
            {
                double v = (double)r / (double)(getTileSize() - 1);
                double y = ymin + v * (ymax - ymin);
                if (metaImage.readAtCoord(pixel, x, y))
                {
                    hf->setHeight(c, r, decode(pixel));
                }
            }
        }

        return GeoHeightField(hf.release(), key.getExtent());
    }

    else
    {
        // Make an image, then convert it to a heightfield
        GeoImage geoImage = _imageLayer->createImageImplementation(key, progress);
        if (geoImage.valid())
        {
            const osg::Image* image = geoImage.getImage();

            // Allocate the heightfield.
            osg::HeightField* hf = new osg::HeightField();
            hf->allocate(image->s(), image->t());

            ImageUtils::PixelReader reader(image);
            osg::Vec4f pixel;

            for (int c = 0; c < image->s(); c++)
            {
                for (int r = 0; r < image->t(); r++)
                {
                    reader(pixel, c, r);
                    hf->setHeight(c, r, decode(pixel));
                }
            }

            return GeoHeightField(hf, key.getExtent());
        }

        return GeoHeightField::INVALID;
    }
}
