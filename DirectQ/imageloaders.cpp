/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"


// palette hackery
palettedef_t d3d_QuakePalette;

float gc_miptable[256];

void D3DImage_MakeQuakePalettes (byte *palette)
{
	int dark = 21024;
	int darkindex = 0;

	for (int i = 0; i < 256; i++)
	{
		float f = (float) i / 255.0f;

		gc_miptable[i] = (f * f) * 0.25f;
	}

	for (int i = 0; i < 256; i++, palette += 3)
	{
		// on disk palette has 3 components
		byte *rgb = palette;

		if (i < 255)
		{
			int darkness = ((int) rgb[0] + (int) rgb[1] + (int) rgb[2]) / 3;

			if (darkness < dark)
			{
				dark = darkness;
				darkindex = i;
			}
		}
		else rgb[0] = rgb[1] = rgb[2] = 0;

		// set correct alpha colour
		byte alpha = (i < 255) ? 255 : 0;

		d3d_QuakePalette.standard11[i] = (alpha << 24) | (rgb[0] << 0) | (rgb[1] << 8) | (rgb[2] << 16);

		if (i > 223)
			d3d_QuakePalette.luma11[i] = (alpha << 24) | (rgb[0] << 0) | (rgb[1] << 8) | (rgb[2] << 16);
		else d3d_QuakePalette.luma11[i] = (alpha << 24);
	}

	// set index of darkest colour
	d3d_QuakePalette.darkindex = darkindex;
}


int D3DImage_PowerOf2Size (int size)
{
	size--;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size++;

	return size;
}


void D3DImage_CompressRowPitch (unsigned *data, int width, int height, int pitch)
{
	if (width != pitch)
	{
		unsigned *out = data;

		// as a minor optimization we can skip the first row
		// since out and data point to the same this is OK
		out += width;
		data += pitch;

		for (int h = 1; h < height; h++)
		{
			for (int w = 0; w < width; w++)
				out[w] = data[w];

			out += width;
			data += pitch;
		}
	}
}


int D3DImage_AverageMipGC (int _1, int _2, int _3, int _4)
{
	// gc_miptable is pre-squared and pre-quartered to reduce calculations for faster loading times
	float avg = 255.0f * sqrt (gc_miptable[_1] + gc_miptable[_2] + gc_miptable[_3] + gc_miptable[_4]);

	if (avg < 0.0f) return 0;
	if (avg > 254.5f) return 255;

	return (int) (avg + 0.5f);
}


// can operate in-place or not
void D3DImage_MipMap (byte *in, byte *out, int width, int height)
{
	int i, j;

	width <<= 2;
	height >>= 1;

	for (i = 0; i < height; i++, in += width)
	{
		for (j = 0; j < width; j += 8, out += 4, in += 8)
		{
			out[0] = D3DImage_AverageMipGC (in[0], in[4], in[width + 0], in[width + 4]);
			out[1] = D3DImage_AverageMipGC (in[1], in[5], in[width + 1], in[width + 5]);
			out[2] = D3DImage_AverageMipGC (in[2], in[6], in[width + 2], in[width + 6]);
			out[3] = D3DImage_AverageMipGC (in[3], in[7], in[width + 3], in[width + 7]);
		}
	}
}


void D3DImage_Resample (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
{
	int		i, j;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	*p1, *p2;
	byte		*pix1, *pix2, *pix3, *pix4;

	if (outwidth < 1) outwidth = 1;
	if (outheight < 1) outheight = 1;

	if (inwidth == outwidth && inheight == outheight)
	{
		memcpy (out, in, inwidth * inheight * 4);
		return;
	}

	int hunkmark = TempHunk->GetLowMark ();

	p1 = (unsigned *) TempHunk->FastAlloc (outwidth * 4);
	p2 = (unsigned *) TempHunk->FastAlloc (outwidth * 4);

	fracstep = inwidth * 0x10000 / outwidth;
	frac = fracstep >> 2;

	for (i = 0; i < outwidth; i++)
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	frac = 3 * (fracstep >> 2);

	for (i = 0; i < outwidth; i++)
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++, out += outwidth)
	{
		inrow = in + inwidth * (int) ((i + 0.25) * inheight / outheight);
		inrow2 = in + inwidth * (int) ((i + 0.75) * inheight / outheight);
		frac = fracstep >> 1;

		for (j = 0; j < outwidth; j++)
		{
			pix1 = (byte *) inrow + p1[j];
			pix2 = (byte *) inrow + p2[j];
			pix3 = (byte *) inrow2 + p1[j];
			pix4 = (byte *) inrow2 + p2[j];

			((byte *) (out + j))[0] = D3DImage_AverageMipGC (pix1[0], pix2[0], pix3[0], pix4[0]);
			((byte *) (out + j))[1] = D3DImage_AverageMipGC (pix1[1], pix2[1], pix3[1], pix4[1]);
			((byte *) (out + j))[2] = D3DImage_AverageMipGC (pix1[2], pix2[2], pix3[2], pix4[2]);
			((byte *) (out + j))[3] = D3DImage_AverageMipGC (pix1[3], pix2[3], pix3[3], pix4[3]);
		}
	}

	TempHunk->FreeToLowMark (hunkmark);
}


void D3DImage_AlphaEdgeFix (byte *data, int width, int height)
{
	int i, j, n = 0, b, c[3] = {0, 0, 0}, lastrow, thisrow, nextrow, lastpix, thispix, nextpix;
	byte *dest = data;

	for (i = 0; i < height; i++)
	{
		lastrow = width * 4 * ((i == 0) ? height - 1 : i - 1);
		thisrow = width * 4 * i;
		nextrow = width * 4 * ((i == height - 1) ? 0 : i + 1);

		for (j = 0; j < width; j++, dest += 4)
		{
			if (dest[3]) // not transparent
				continue;

			lastpix = 4 * ((j == 0) ? width - 1 : j - 1);
			thispix = 4 * j;
			nextpix = 4 * ((j == width - 1) ? 0 : j + 1);

			b = lastrow + lastpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = thisrow + lastpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = nextrow + lastpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = lastrow + thispix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = nextrow + thispix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = lastrow + nextpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = thisrow + nextpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = nextrow + nextpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}

			// average all non-transparent neighbors
			if (n)
			{
				dest[0] = (byte) (c[0] / n);
				dest[1] = (byte) (c[1] / n);
				dest[2] = (byte) (c[2] / n);

				n = c[0] = c[1] = c[2] = 0;
			}
		}
	}
}


void D3DImage_AlphaMask (byte *data, int size)
{
	for (int i = 0; i < size; i++, data += 4)
	{
		byte best = 0;

		if (data[0] > best) best = data[0];
		if (data[1] > best) best = data[1];
		if (data[2] > best) best = data[2];

		data[0] = best;
		data[1] = best;
		data[2] = best;
		data[3] = best;
	}
}


typedef struct pcx_s
{
	char	manufacturer;
	char	version;
	char	encoding;
	char	bits_per_pixel;
	unsigned short	xmin, ymin, xmax, ymax;
	unsigned short	hres, vres;
	unsigned char	palette[48];
	char	reserved;
	char	color_planes;
	unsigned short	bytes_per_line;
	unsigned short	palette_type;
	char	filler[58];
} pcx_t;


byte *D3DImage_LoadPCX (byte *f, int *width, int *height)
{
	pcx_t	pcx;
	byte	*palette, *a, *b, *image_rgba, *fin, *pbuf, *enddata;
	int		x, y, x2, dataByte;

	if (CQuakeFile::FileSize < sizeof (pcx) + 768) return NULL;

	fin = f;

	memcpy (&pcx, fin, sizeof (pcx));
	fin += sizeof (pcx);

	if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8) return NULL;

	*width = pcx.xmax + 1;
	*height = pcx.ymax + 1;

	palette = f + CQuakeFile::FileSize - 768;

	if ((image_rgba = (byte *) TempHunk->FastAlloc ((*width) * (*height) * 4)) == NULL) return NULL;

	pbuf = image_rgba + (*width) * (*height) * 3;
	enddata = palette;

	for (y = 0; y < (*height) && fin < enddata; y++)
	{
		a = pbuf + y * (*width);

		for (x = 0; x < (*width) && fin < enddata;)
		{
			dataByte = *fin++;

			if (dataByte >= 0xC0)
			{
				if (fin >= enddata) break;

				x2 = x + (dataByte & 0x3F);
				dataByte = *fin++;

				if (x2 > (*width)) x2 = (*width);
				while (x < x2) a[x++] = dataByte;
			}
			else a[x++] = dataByte;
		}

		while (x < (*width))
			a[x++] = 0;
	}

	a = image_rgba;
	b = pbuf;

	for (x = 0; x < (*width) * (*height); x++)
	{
		y = *b++ * 3;

		*a++ = palette[y];
		*a++ = palette[y + 1];
		*a++ = palette[y + 2];
		*a++ = 255;
	}

	return image_rgba;
}


typedef struct tgaheader_s
{
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} tgaheader_t;


byte *D3DImage_LoadTGA (byte *f, int *width, int *height)
{
	if (CQuakeFile::FileSize < 18 + 3) return NULL;

	byte *pixbuf, *image_rgba;
	tgaheader_t header;

	memset (&header, 0, sizeof (tgaheader_t));

	header.id_length = f[0];
	header.colormap_type = f[1];
	header.image_type = f[2];

	header.colormap_index = f[3] + f[4] * 256;
	header.colormap_length = f[5] + f[6] * 256;
	header.colormap_size = f[7];
	header.x_origin = f[8] + f[9] * 256;
	header.y_origin = f[10] + f[11] * 256;
	header.width = f[12] + f[13] * 256;
	header.height = f[14] + f[15] * 256;

	header.pixel_size = f[16];
	header.attributes = f[17];

	if (header.image_type != 2 && header.image_type != 10) return NULL;
	if (header.colormap_type != 0 || (header.pixel_size != 32 && header.pixel_size != 24)) return NULL;

	byte *enddata = f + CQuakeFile::FileSize;
	int columns = header.width;
	int rows = header.height;
	bool upside_down = !(header.attributes & 0x20); // johnfitz -- fix for upside-down targas
	int realrow;
	byte *fin = f + 18 + header.id_length;

	*width = columns;
	*height = rows;

	if (header.image_type == 2)
	{
		if (header.pixel_size == 24)
		{
			if ((image_rgba = (byte *) TempHunk->FastAlloc (columns * rows * 4)) == NULL)
			{
				Con_Printf ("LoadTGA: not enough memory for %i by %i image\n", columns, rows);
				return NULL;
			}

			pixbuf = fin;
			fin = image_rgba;

			for (int i = 0; i < columns * rows; i++, pixbuf += 3, image_rgba += 4)
			{
				image_rgba[0] = pixbuf[2];
				image_rgba[1] = pixbuf[1];
				image_rgba[2] = pixbuf[0];
				image_rgba[3] = 255;
			}
		}
		else
		{
			pixbuf = fin;

			for (int i = 0; i < columns * rows; i++, pixbuf += 4)
			{
				byte tmp = pixbuf[2];
				pixbuf[2] = pixbuf[0];
				pixbuf[0] = tmp;
			}
		}

		if (upside_down)
		{
			unsigned *texels = (unsigned *) fin;

			for (int x = 0; x < columns; x++)
			{
				for (int y = 0; y < (rows / 2); y++)
				{
					int pos1 = y * columns + x;
					int pos2 = (rows - 1 - y) * columns + x;
					unsigned int temp = texels[pos1];

					texels[pos1] = texels[pos2];
					texels[pos2] = temp;
				}
			}
		}

		return fin;
	}
	else if (header.image_type == 10)
	{
		// Runlength encoded RGB images
		unsigned char red = 0, green = 0, blue = 0, alphabyte = 0, packetHeader, packetSize, j;

		if ((image_rgba = (byte *) TempHunk->FastAlloc (columns * rows * 4)) == NULL)
		{
			Con_Printf ("LoadTGA: not enough memory for %i by %i image\n", columns, rows);
			return NULL;
		}

		for (int row = rows - 1; row >= 0; row--)
		{
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = image_rgba + realrow * columns * 4;

			for (int column = 0; column < columns;)
			{
				if (fin >= enddata)
					goto outofdata;

				packetHeader = *fin++;
				packetSize = 1 + (packetHeader & 0x7f);

				if (packetHeader & 0x80)
				{
					// run-length packet
					switch (header.pixel_size)
					{
					case 24:
						if (fin + 3 > enddata)
							goto outofdata;

						blue = *fin++;
						green = *fin++;
						red = *fin++;
						alphabyte = 255;
						break;

					case 32:
						if (fin + 4 > enddata)
							goto outofdata;

						blue = *fin++;
						green = *fin++;
						red = *fin++;
						alphabyte = *fin++;
						break;
					}

					for (j = 0; j < packetSize; j++)
					{
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						column++;

						if (column == columns)
						{
							// run spans across rows
							column = 0;

							if (row > 0)
								row--;
							else goto breakOut;

							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = image_rgba + realrow * columns * 4;
						}
					}
				}
				else
				{
					// non run-length packet
					for (j = 0; j < packetSize; j++)
					{
						switch (header.pixel_size)
						{
						case 24:
							if (fin + 3 > enddata)
								goto outofdata;

							*pixbuf++ = fin[2];
							*pixbuf++ = fin[1];
							*pixbuf++ = fin[0];
							*pixbuf++ = 255;
							fin += 3;
							break;

						case 32:
							if (fin + 4 > enddata)
								goto outofdata;

							*pixbuf++ = fin[2];
							*pixbuf++ = fin[1];
							*pixbuf++ = fin[0];
							*pixbuf++ = fin[3];
							fin += 4;
							break;
						}

						column++;

						if (column == columns)
						{
							// pixel packet run spans across rows
							column = 0;

							if (row > 0)
								row--;
							else goto breakOut;

							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = image_rgba + realrow * columns * 4;
						}
					}
				}
			}

breakOut:;
		}
	}

outofdata:;
	return image_rgba;
}

