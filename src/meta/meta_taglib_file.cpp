/*
 * meta_taglib_file.cpp - File Stream used for file reading with taglib
 *
 * Copyright (c) 2014   A. Dilly
 *
 * AirCat is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * AirCat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with AirCat.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "meta_taglib_file.h"

#define BUFFER_SIZE 8192

#if (TAGLIB_MAJOR_VERSION >= 1) &&  (TAGLIB_MINOR_VERSION >= 9)

MetaTaglibFile::MetaTaglibFile(const std::string& openFileName,
                               bool openReadOnly)
{
    /* Init values */
    isReadOnly = true;
    fileName = openFileName;

    /* Open file */
    file = fs_open(fileName.c_str(), O_RDONLY, 0);
}

MetaTaglibFile::~MetaTaglibFile()
{
    /* Close file */
    fs_close(file);
}

TagLib::FileName MetaTaglibFile::name() const
{
    return fileName.c_str();
}

TagLib::ByteVector MetaTaglibFile::readBlock(TagLib::ulong length)
{
    if(!isOpen() || length == 0)
        return TagLib::ByteVector::null;

    const TagLib::ulong streamLength =
            static_cast<TagLib::ulong>(MetaTaglibFile::length());
    if(length > bufferSize() && length > streamLength)
    length = streamLength;

    TagLib::ByteVector buffer(static_cast<TagLib::uint>(length));

    const size_t count = fs_read(file, buffer.data(), buffer.size());
    buffer.resize(static_cast<TagLib::uint>(count));

    return buffer;
}

void MetaTaglibFile::writeBlock(const TagLib::ByteVector &data)
{
    /* Not yet implemented */
}

void MetaTaglibFile::insert(const TagLib::ByteVector &data, TagLib::ulong start,
                            TagLib::ulong replace)
{
    /* Not yet implemented */
}

void MetaTaglibFile::removeBlock(TagLib::ulong start, TagLib::ulong length)
{
    /* Not yet implemented */
}

bool MetaTaglibFile::readOnly() const
{
    return isReadOnly;
}

bool MetaTaglibFile::isOpen() const
{
    return (file != NULL);
}

void MetaTaglibFile::seek(long offset, TagLib::IOStream::Position p)
{
    int whence;

    /* Get whence */
    switch(p)
    {
        case Beginning:
            whence = SEEK_SET;
            break;
        case Current:
            whence = SEEK_CUR;
            break;
        case End:
            whence = SEEK_END;
            break;
        default:
            return;
    }

    /* Seek in file */
    fs_lseek(file, offset, whence);
}

void MetaTaglibFile::clear()
{
    /* Nothing to do */
}

long MetaTaglibFile::tell() const
{
    /* Get current position */
    return fs_lseek(file, 0 , SEEK_CUR);
}

long MetaTaglibFile::length()
{
    struct stat st;

    /* Stat file to get its length */
    if(fs_fstat(file, &st) != 0)
        return -1;

    /* Get size */
    return st.st_size;
}

void MetaTaglibFile::truncate(long length)
{
    /* Truncate file */
    fs_ftruncate(file, length);
}

TagLib::uint MetaTaglibFile::bufferSize()
{
    return BUFFER_SIZE;
}

#endif

