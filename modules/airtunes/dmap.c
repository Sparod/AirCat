/*
 * dmap.c - A Tiny DMAP parser
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dmap.h"

#define TO16(b) ((b)[0] << 8 & 0xFF00) | ((b)[1] & 0x00FF)
#define TO32(b) ((b)[0] << 24 & 0xFF000000) | ((b)[1] << 16 & 0x00FF0000) | \
		((b)[2] << 8 & 0x0000FF00) | ((b)[3] & 0x000000FF)
#define TO64(b) ((uint64_t)(b)[0] << 56 & 0xFF00000000000000) | \
		((uint64_t)(b)[1] << 48 & 0x00FF000000000000) | \
		((uint64_t)(b)[2] << 40 & 0x0000FF0000000000) | \
		((uint64_t)(b)[3] << 32 & 0x000000FF00000000) | \
		((b)[4] << 24 & 0x00000000FF000000) | \
		((b)[5] << 16 & 0x0000000000FF0000) | \
		((b)[6] << 8 & 0x000000000000FF00) | \
		((b)[7] & 0x00000000000000FF)

static struct dmap_tag {
	const char *tag;
	enum dmap_type type;
	const char *full_tag;
} dmap_tags[] = {
	{"abal", DMAP_CONT, "daap.browsealbumlisting"},
	{"abar", DMAP_CONT, "daap.browseartistlisting"},
	{"abcp", DMAP_CONT, "daap.browsecomposerlisting"},
	{"abgn", DMAP_CONT, "daap.browsegenrelisting"},
	{"abpl", DMAP_UINT, "daap.baseplaylist"},
	{"abro", DMAP_CONT, "daap.databasebrowse"},
	{"adbs", DMAP_CONT, "daap.databasesongs"},
	{"aeAD", DMAP_CONT, "com.apple.itunes.adam-ids-array"},
	{"aeAI", DMAP_UINT, "com.apple.itunes.itms-artistid"},
	{"aeCD", DMAP_STR,  "com.apple.itunes.flat-chapter-data"},
	{"aeCF", DMAP_UINT, "com.apple.itunes.cloud-flavor-id"},
	{"aeCI", DMAP_UINT, "com.apple.itunes.itms-composerid"},
	{"aeCK", DMAP_UINT, "com.apple.itunes.cloud-library-kind"},
	{"aeCM", DMAP_UINT, "com.apple.itunes.cloud-status"},
	{"aeCR", DMAP_STR,  "com.apple.itunes.content-rating"} ,
	{"aeCS", DMAP_UINT, "com.apple.itunes.artworkchecksum"},
	{"aeCU", DMAP_UINT, "com.apple.itunes.cloud-user-id"},
	{"aeCd", DMAP_UINT, "com.apple.itunes.cloud-id"},
	{"aeDP", DMAP_UINT, "com.apple.itunes.drm-platform-id"},
	{"aeDR", DMAP_UINT, "com.apple.itunes.drm-user-id"},
	{"aeDV", DMAP_UINT, "com.apple.itunes.drm-versions"},
	{"aeEN", DMAP_STR,  "com.apple.itunes.episode-num-str"},
	{"aeES", DMAP_UINT, "com.apple.itunes.episode-sort"},
	{"aeGD", DMAP_UINT, "com.apple.itunes.gapless-enc-dr"} ,
	{"aeGE", DMAP_UINT, "com.apple.itunes.gapless-enc-del"},
	{"aeGH", DMAP_UINT, "com.apple.itunes.gapless-heur"},
	{"aeGI", DMAP_UINT, "com.apple.itunes.itms-genreid"},
	{"aeGR", DMAP_UINT, "com.apple.itunes.gapless-resy"},
	{"aeGU", DMAP_UINT, "com.apple.itunes.gapless-dur"},
	{"aeGs", DMAP_UINT, "com.apple.itunes.can-be-genius-seed"},
	{"aeHC", DMAP_UINT, "com.apple.itunes.has-chapter-data"},
	{"aeHD", DMAP_UINT, "com.apple.itunes.is-hd-video"},
	{"aeHV", DMAP_UINT, "com.apple.itunes.has-video"},
	{"aeK1", DMAP_UINT, "com.apple.itunes.drm-key1-id"},
	{"aeK2", DMAP_UINT, "com.apple.itunes.drm-key2-id"},
	{"aeMC", DMAP_UINT,
			 "com.apple.itunes.playlist-contains-media-type-count"},
	{"aeMK", DMAP_UINT, "com.apple.itunes.mediakind"},
	{"aeMX", DMAP_STR,  "com.apple.itunes.movie-info-xml"},
	{"aeMk", DMAP_UINT, "com.apple.itunes.extended-media-kind"},
	{"aeND", DMAP_UINT, "com.apple.itunes.non-drm-user-id"},
	{"aeNN", DMAP_STR,  "com.apple.itunes.network-name"},
	{"aeNV", DMAP_UINT, "com.apple.itunes.norm-volume"},
	{"aePC", DMAP_UINT, "com.apple.itunes.is-podcast"},
	{"aePI", DMAP_UINT, "com.apple.itunes.itms-playlistid"},
	{"aePP", DMAP_UINT, "com.apple.itunes.is-podcast-playlist"},
	{"aePS", DMAP_UINT, "com.apple.itunes.special-playlist"},
	{"aeRD", DMAP_UINT, "com.apple.itunes.rental-duration"},
	{"aeRP", DMAP_UINT, "com.apple.itunes.rental-pb-start"},
	{"aeRS", DMAP_UINT, "com.apple.itunes.rental-start"},
	{"aeRU", DMAP_UINT, "com.apple.itunes.rental-pb-duration"},
	{"aeRf", DMAP_UINT, "com.apple.itunes.is-featured"},
	{"aeSE", DMAP_UINT, "com.apple.itunes.store-pers-id"},
	{"aeSF", DMAP_UINT, "com.apple.itunes.itms-storefrontid"},
	{"aeSG", DMAP_UINT, "com.apple.itunes.saved-genius"},
	{"aeSI", DMAP_UINT, "com.apple.itunes.itms-songid"},
	{"aeSN", DMAP_STR,  "com.apple.itunes.series-name"},
	{"aeSP", DMAP_UINT, "com.apple.itunes.smart-playlist"},
	{"aeSU", DMAP_UINT, "com.apple.itunes.season-num"},
	{"aeSV", DMAP_VER,  "com.apple.itunes.music-sharing-version"},
	{"aeXD", DMAP_STR,  "com.apple.itunes.xid"},
	{"aecp", DMAP_STR,  "com.apple.itunes.collection-description"},
	{"aels", DMAP_UINT, "com.apple.itunes.liked-state"},
	{"aemi", DMAP_CONT, "com.apple.itunes.media-kind-listing-item"},
	{"aeml", DMAP_CONT, "com.apple.itunes.media-kind-listing"},
	{"agac", DMAP_UINT, "daap.groupalbumcount"},
	{"agma", DMAP_UINT, "daap.groupmatchedqueryalbumcount"},
	{"agmi", DMAP_UINT, "daap.groupmatchedqueryitemcount"},
	{"agrp", DMAP_STR,  "daap.songgrouping"},
	{"aply", DMAP_CONT, "daap.databaseplaylists"},
	{"aprm", DMAP_UINT, "daap.playlistrepeatmode"},
	{"apro", DMAP_VER,  "daap.protocolversion"},
	{"apsm", DMAP_UINT, "daap.playlistshufflemode"},
	{"apso", DMAP_CONT, "daap.playlistsongs"},
	{"arif", DMAP_CONT, "daap.resolveinfo"},
	{"arsv", DMAP_CONT, "daap.resolve"},
	{"asaa", DMAP_STR,  "daap.songalbumartist"},
	{"asac", DMAP_UINT, "daap.songartworkcount"},
	{"asai", DMAP_UINT, "daap.songalbumid"},
	{"asal", DMAP_STR,  "daap.songalbum"},
	{"asar", DMAP_STR,  "daap.songartist"},
	{"asas", DMAP_UINT, "daap.songalbumuserratingstatus"},
	{"asbk", DMAP_UINT, "daap.bookmarkable"},
	{"asbo", DMAP_UINT, "daap.songbookmark"},
	{"asbr", DMAP_UINT, "daap.songbitrate"},
	{"asbt", DMAP_UINT, "daap.songbeatsperminute"},
	{"ascd", DMAP_UINT, "daap.songcodectype"},
	{"ascm", DMAP_STR,  "daap.songcomment"},
	{"ascn", DMAP_STR,  "daap.songcontentdescription"},
	{"asco", DMAP_UINT, "daap.songcompilation"},
	{"ascp", DMAP_STR,  "daap.songcomposer"},
	{"ascr", DMAP_UINT, "daap.songcontentrating"},
	{"ascs", DMAP_UINT, "daap.songcodecsubtype"},
	{"asct", DMAP_STR,  "daap.songcategory"},
	{"asda", DMAP_DATE, "daap.songdateadded"},
	{"asdb", DMAP_UINT, "daap.songdisabled"},
	{"asdc", DMAP_UINT, "daap.songdisccount"},
	{"asdk", DMAP_UINT, "daap.songdatakind"},
	{"asdm", DMAP_DATE, "daap.songdatemodified"},
	{"asdn", DMAP_UINT, "daap.songdiscnumber"},
	{"asdp", DMAP_DATE, "daap.songdatepurchased"},
	{"asdr", DMAP_DATE, "daap.songdatereleased"},
	{"asdt", DMAP_STR,  "daap.songdescription"},
	{"ased", DMAP_UINT, "daap.songextradata"},
	{"aseq", DMAP_STR,  "daap.songeqpreset"},
	{"ases", DMAP_UINT, "daap.songexcludefromshuffle"},
	{"asfm", DMAP_STR,  "daap.songformat"},
	{"asgn", DMAP_STR,  "daap.songgenre"},
	{"asgp", DMAP_UINT, "daap.songgapless"},
	{"asgr", DMAP_UINT, "daap.supportsgroups"},
	{"ashp", DMAP_UINT, "daap.songhasbeenplayed"},
	{"askd", DMAP_DATE, "daap.songlastskipdate"},
	{"askp", DMAP_UINT, "daap.songuserskipcount"},
	{"asky", DMAP_STR,  "daap.songkeywords"},
	{"aslc", DMAP_STR,  "daap.songlongcontentdescription"},
	{"aslr", DMAP_UINT, "daap.songalbumuserrating"},
	{"asls", DMAP_UINT, "daap.songlongsize"},
	{"aspc", DMAP_UINT, "daap.songuserplaycount"},
	{"aspl", DMAP_DATE, "daap.songdateplayed"},
	{"aspu", DMAP_STR,  "daap.songpodcasturl"},
	{"asri", DMAP_UINT, "daap.songartistid"},
	{"asrs", DMAP_UINT, "daap.songuserratingstatus"},
	{"asrv", DMAP_UINT, "daap.songrelativevolume"},
	{"assa", DMAP_STR,  "daap.sortartist"},
	{"assc", DMAP_STR,  "daap.sortcomposer"},
	{"assl", DMAP_STR,  "daap.sortalbumartist"},
	{"assn", DMAP_STR,  "daap.sortname"},
	{"assp", DMAP_UINT, "daap.songstoptime"},
	{"assr", DMAP_UINT, "daap.songsamplerate"},
	{"asss", DMAP_STR,  "daap.sortseriesname"},
	{"asst", DMAP_UINT, "daap.songstarttime"},
	{"assu", DMAP_STR,  "daap.sortalbum"},
	{"assz", DMAP_UINT, "daap.songsize"},
	{"astc", DMAP_UINT, "daap.songtrackcount"},
	{"astm", DMAP_UINT, "daap.songtime"},
	{"astn", DMAP_UINT, "daap.songtracknumber"},
	{"asul", DMAP_STR,  "daap.songdataurl"},
	{"asur", DMAP_UINT, "daap.songuserrating"},
	{"asvc", DMAP_UINT, "daap.songprimaryvideocodec"},
	{"asyr", DMAP_UINT, "daap.songyear"},
	{"ated", DMAP_UINT, "daap.supportsextradata"},
	{"avdb", DMAP_CONT, "daap.serverdatabases"},
	{"caar", DMAP_UINT, "dacp.availablerepeatstates"},
	{"caas", DMAP_UINT, "dacp.availableshufflestates"},
	{"caci", DMAP_CONT, "caci"},
	{"cafe", DMAP_UINT, "dacp.fullscreenenabled"},
	{"cafs", DMAP_UINT, "dacp.fullscreen"},
	{"caia", DMAP_UINT, "dacp.isactive"},
	{"cana", DMAP_STR,  "dacp.nowplayingartist"},
	{"cang", DMAP_STR,  "dacp.nowplayinggenre"},
	{"canl", DMAP_STR,  "dacp.nowplayingalbum"},
	{"cann", DMAP_STR,  "dacp.nowplayingname"},
	{"canp", DMAP_UINT, "dacp.nowplayingids"},
	{"cant", DMAP_UINT, "dacp.nowplayingtime"},
	{"capr", DMAP_VER,  "dacp.protocolversion"},
	{"caps", DMAP_UINT, "dacp.playerstate"},
	{"carp", DMAP_UINT, "dacp.repeatstate"},
	{"cash", DMAP_UINT, "dacp.shufflestate"},
	{"casp", DMAP_CONT, "dacp.speakers"},
	{"cast", DMAP_UINT, "dacp.songtime"},
	{"cavc", DMAP_UINT, "dacp.volumecontrollable"},
	{"cave", DMAP_UINT, "dacp.visualizerenabled"},
	{"cavs", DMAP_UINT, "dacp.visualizer"},
	{"ceJC", DMAP_UINT, "com.apple.itunes.jukebox-client-vote"},
	{"ceJI", DMAP_UINT, "com.apple.itunes.jukebox-current"},
	{"ceJS", DMAP_UINT, "com.apple.itunes.jukebox-score"},
	{"ceJV", DMAP_UINT, "com.apple.itunes.jukebox-vote"},
	{"ceQR", DMAP_CONT, "com.apple.itunes.playqueue-contents-response"},
	{"ceQa", DMAP_STR,  "com.apple.itunes.playqueue-album"},
	{"ceQg", DMAP_STR,  "com.apple.itunes.playqueue-genre"},
	{"ceQn", DMAP_STR,  "com.apple.itunes.playqueue-name"},
	{"ceQr", DMAP_STR,  "com.apple.itunes.playqueue-artist"},
	{"cmgt", DMAP_CONT, "dmcp.getpropertyresponse"},
	{"cmmk", DMAP_UINT, "dmcp.mediakind"},
	{"cmpr", DMAP_VER,  "dmcp.protocolversion"},
	{"cmsr", DMAP_UINT, "dmcp.serverrevision"},
	{"cmst", DMAP_CONT, "dmcp.playstatus"},
	{"cmvo", DMAP_UINT, "dmcp.volume"},
	{"f?ch", DMAP_UINT, "dmap.haschildcontainers"},
	{"ipsa", DMAP_CONT, "dpap.iphotoslideshowadvancedoptions"},
	{"ipsl", DMAP_CONT, "dpap.iphotoslideshowoptions"},
	{"mbcl", DMAP_CONT, "dmap.bag"},
	{"mccr", DMAP_CONT, "dmap.contentcodesresponse"},
	{"mcna", DMAP_STR,  "dmap.contentcodesname"},
	{"mcnm", DMAP_UINT, "dmap.contentcodesnumber"},
	{"mcon", DMAP_CONT, "dmap.container"},
	{"mctc", DMAP_UINT, "dmap.containercount"},
	{"mcti", DMAP_UINT, "dmap.containeritemid"},
	{"mcty", DMAP_UINT, "dmap.contentcodestype"},
	{"mdbk", DMAP_UINT, "dmap.databasekind"},
	{"mdcl", DMAP_CONT, "dmap.dictionary"},
	{"mdst", DMAP_UINT, "dmap.downloadstatus"},
	{"meds", DMAP_UINT, "dmap.editcommandssupported"},
	{"miid", DMAP_UINT, "dmap.itemid"},
	{"mikd", DMAP_UINT, "dmap.itemkind"},
	{"mimc", DMAP_UINT, "dmap.itemcount"},
	{"minm", DMAP_STR,  "dmap.itemname"},
	{"mlcl", DMAP_CONT, "dmap.listing"},
	{"mlid", DMAP_UINT, "dmap.sessionid"},
	{"mlit", DMAP_CONT, "dmap.listingitem"},
	{"mlog", DMAP_CONT, "dmap.loginresponse"},
	{"mpco", DMAP_UINT, "dmap.parentcontainerid"},
	{"mper", DMAP_UINT, "dmap.persistentid"},
	{"mpro", DMAP_VER,  "dmap.protocolversion"},
	{"mrco", DMAP_UINT, "dmap.returnedcount"},
	{"mrpr", DMAP_UINT, "dmap.remotepersistentid"},
	{"msal", DMAP_UINT, "dmap.supportsautologout"},
	{"msas", DMAP_UINT, "dmap.authenticationschemes"},
	{"msau", DMAP_UINT, "dmap.authenticationmethod"},
	{"msbr", DMAP_UINT, "dmap.supportsbrowse"},
	{"msdc", DMAP_UINT, "dmap.databasescount"},
	{"msex", DMAP_UINT, "dmap.supportsextensions"},
	{"msix", DMAP_UINT, "dmap.supportsindex"},
	{"mslr", DMAP_UINT, "dmap.loginrequired"},
	{"msma", DMAP_UINT, "dmap.machineaddress"},
	{"msml", DMAP_CONT, "msml"},
	{"mspi", DMAP_UINT, "dmap.supportspersistentids"},
	{"msqy", DMAP_UINT, "dmap.supportsquery"},
	{"msrs", DMAP_UINT, "dmap.supportsresolve"},
	{"msrv", DMAP_CONT, "dmap.serverinforesponse"},
	{"mstc", DMAP_DATE, "dmap.utctime"},
	{"mstm", DMAP_UINT, "dmap.timeoutinterval"},
	{"msto", DMAP_UINT, "dmap.utcoffset"},
	{"msts", DMAP_STR,  "dmap.statusstring"},
	{"mstt", DMAP_UINT, "dmap.status"},
	{"msup", DMAP_UINT, "dmap.supportsupdate"},
	{"mtco", DMAP_UINT, "dmap.specifiedtotalcount"},
	{"mudl", DMAP_CONT, "dmap.deletedidlisting"},
	{"mupd", DMAP_CONT, "dmap.updateresponse"},
	{"musr", DMAP_UINT, "dmap.serverrevision"},
	{"muty", DMAP_UINT, "dmap.updatetype"},
	{"pasp", DMAP_STR,  "dpap.aspectratio"},
	{"pcmt", DMAP_STR,  "dpap.imagecomments"},
	{"peak", DMAP_UINT, "com.apple.itunes.photos.album-kind"},
	{"peed", DMAP_DATE, "com.apple.itunes.photos.exposure-date"},
	{"pefc", DMAP_CONT, "com.apple.itunes.photos.faces"},
	{"peki", DMAP_UINT, "com.apple.itunes.photos.key-image-id"},
	{"pekm", DMAP_CONT, "com.apple.itunes.photos.key-image"},
	{"pemd", DMAP_DATE, "com.apple.itunes.photos.modification-date"},
	{"pfai", DMAP_CONT, "dpap.failureids"},
	{"pfdt", DMAP_CONT, "dpap.filedata"},
	{"pfmt", DMAP_STR,  "dpap.imageformat"},
	{"phgt", DMAP_UINT, "dpap.imagepixelheight"},
	{"picd", DMAP_DATE, "dpap.creationdate"},
	{"pifs", DMAP_UINT, "dpap.imagefilesize"},
	{"pimf", DMAP_STR,  "dpap.imagefilename"},
	{"plsz", DMAP_UINT, "dpap.imagelargefilesize"},
	{"ppro", DMAP_VER,  "dpap.protocolversion"},
	{"prat", DMAP_UINT, "dpap.imagerating"},
	{"pret", DMAP_CONT, "dpap.retryids"},
	{"pwth", DMAP_UINT, "dpap.imagepixelwidth"}
};
static int dmap_tags_count = sizeof(dmap_tags) / sizeof(struct dmap_tag);

struct dmap {
	/* Callbacks */
	dmap_cb cb;
	dmap_in_cb in_cb;
	dmap_out_cb out_cb;
	void *user_data;
	/* Current item */
	unsigned char header[8];
	unsigned char header_len;
	unsigned char *buffer;
	size_t len;
	size_t i_len;
	enum dmap_type type;
	const char *tag;
	const char *full_tag;
	/* Containers */
	struct {
		const char *tag;
		const char *full_tag;
		size_t len;
	} containers[DMAP_MAX_DEPTH];
	unsigned long container;
};

struct dmap *dmap_init(dmap_cb cb, dmap_in_cb in_cb, dmap_out_cb out_cb,
		       void *user_data)
{
	struct dmap *d;

	/* Needs at least main callback */
	if(cb == NULL)
		return NULL;

	/* Allocate context */
	d = malloc(sizeof(struct dmap));
	if(d == NULL)
		return NULL;

	/* Init context */
	d->cb = cb;
	d->in_cb = in_cb;
	d->out_cb = out_cb;
	d->user_data = user_data;
	d->header_len = 0;
	d->buffer = NULL;
	d->len = 0;
	d->container = 0;

	return d;
}

int dmap_parse(struct dmap *d, unsigned char *buffer, size_t len)
{
	unsigned char *data = NULL;
	uint64_t value = 0;
	size_t size = 0;
	int i;

	/* Parse all buffer */
	while(len > 0)
	{
		/* Get header */
		if(d->header_len < 8)
		{
			/* Complete header */
			size = 8 - d->header_len;
			if(size > len)
				size = len;
			memcpy(d->header+d->header_len, buffer, size);
			d->header_len += size;
			if(d->header_len < 8)
				return 0;

			/* Find tag */
			for(i = 0; i < dmap_tags_count; i++)
			{
				if(strncmp(d->header, dmap_tags[i].tag, 4) == 0)
				{
					d->full_tag = dmap_tags[i].full_tag;
					d->type = dmap_tags[i].type;
					d->tag = dmap_tags[i].tag;
					break;
				}
			}

			/* Get length */
			d->i_len = TO32(&d->header[4]);

			/* Update position */
			buffer += size;
			len -= size;
		}

		/* Add container */
		if(d->type == DMAP_CONT && d->container < DMAP_MAX_DEPTH)
		{
			/* Add new container */
			i = d->container;
			d->containers[i].len = d->i_len;
			d->containers[i].tag = d->tag;
			d->containers[i].full_tag = d->full_tag;
			d->container++;

			/* Notice new container */
			if(d->in_cb != NULL)
				d->in_cb(d->user_data, d->tag, d->full_tag);

			goto next;
		}

		/* Not enough data */
		if(d->len + len < d->i_len)
			goto save;

		/* Process item */
		switch(d->type)
		{
			case DMAP_UNKOWN:
			case DMAP_CONT:
				if(d->buffer != NULL)
				{
					data = realloc(d->buffer, d->i_len);
					if(data == NULL)
						return -1;
					memcpy(data+d->len, buffer,
						d->i_len-d->len);
					d->buffer = data;
				}
				else
					data = buffer;
				break;
			case DMAP_DATE:
			case DMAP_VER:
			case DMAP_STR:
				/* Realloc with \0 character */
				data = realloc(d->buffer, d->i_len+1);
				if(data == NULL)
					return -1;
				memcpy(data+d->len, buffer, d->i_len-d->len);
				data[d->i_len] = '\0';
				d->buffer = data;
				break;
			case DMAP_UINT:
				if(d->buffer != NULL)
				{
					size = d->len > d->i_len ? d->i_len :
								   d->len;
					memcpy(d->header, d->buffer, size);
					memcpy(d->header+size, buffer,
					       d->i_len - size);
				}
				else
					memcpy(d->header, buffer, d->i_len);
				switch(d->i_len)
				{
					case 1:
						/* Char */
						value = *buffer;
						break;
					case 2:
						/* Short */
						value = TO16(d->header);
						break;
					case 4:
						/* Long */
						value = TO32(d->header);
						break;
					case 8:
						/* Long long */
						value = TO64(d->header);
						break;
				}
				break;
		}

		/* Parse item */
		d->cb(d->user_data, d->type, d->tag, d->full_tag, data, value,
		      data, d->i_len);

		/* Update position */
		len -= d->i_len - d->len;
		buffer += d->i_len - d->len;

		/* Free internal buffer */
		if(d->buffer != NULL)
		{
			free(d->buffer);
			d->buffer = NULL;
		}
		d->len = 0;

		/* Update container position */
		if(d->container > 0)
		{
			for(i = d->container-1; i >= 0; i--)
			{
				d->containers[i].len -= d->i_len + 8;
				if(d->containers[i].len == 0)
				{
					/* End of container */
					if(d->out_cb != NULL)
					d->out_cb(d->user_data,
						     d->containers[i].tag,
						     d->containers[i].full_tag);
					d->container--;
				}
			}
		}

next:
		/* Reset item status */
		d->full_tag = NULL;
		d->tag = NULL;
		d->header_len = 0;
		d->i_len = 0;
		d->type = 0;
	}

	return 0;

save:
	/* Copy remaining data */
	if(d->buffer != NULL)
	{
		/* Append data */
		data = realloc(d->buffer, d->len + len);
		if(data == NULL)
			return -1;
		d->buffer = data;
	}
	else
	{
		/* Allocate new buffer */
		d->buffer = malloc(len);
		if(d->buffer == NULL)
			return -1;
		d->len = 0;
	}
	memcpy(&d->buffer[d->len], buffer, len);
	d->len += len;
	return 0;
}

void dmap_free(struct dmap *d)
{
	if(d == NULL)
		return;

	/* Free buffer */
	if(d->buffer != NULL)
		free(d->buffer);

	/* Free context */
	free(d);
}

