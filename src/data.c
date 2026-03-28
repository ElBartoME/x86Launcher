/* data.c, Tools to add or manipulate game data for x68Launcher.
 Copyright (C) 2020  John Snowdon
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include <direct.h>
#include <unistd.h>

#include "ini.h"
#ifndef __HAS_DATA
#include "data.h"
#define __HAS_DATA
#endif
#include "fstools.h"
#ifndef __HAS_MAIN
#include "main.h"
#define __HAS_MAIN
#endif

gamedata_t * getGameid(int gameid, gamedata_t *gamedata){
	// Find a given gameid from the list	
	
	while (gamedata != NULL){
		if (gamedata->gameid == gameid){
			return gamedata;
		}
		gamedata = gamedata->next;
	}
	return NULL;
}

gamedata_t * getLastGamedata(gamedata_t *gamedata){
	/* Given a gamedata item, find the last entry of the list */
	
	while (gamedata->next != NULL){
		gamedata = gamedata->next;
	}
	return gamedata;
}

gamedir_t * getLastGameDir(gamedir_t *gamedir){
	/* Given a gamedir search path item, find the last entry of the list */
	
	while (gamedir->next != NULL){
		gamedir = gamedir->next;
	}
	return gamedir;
}

int removeGamedata(gamedata_t *gamedata){
	/* Remove all nodes of a given gamedata list */
	
	gamedata_t * current = gamedata;
	gamedata_t * next = NULL;
	
	if (current->next == NULL){
		if (DATA_VERBOSE){
			printf("%s.%d\t removeGamedata() Freeing gamedata single object\n", __FILE__, __LINE__);	
		}
		free(current);
		gamedata = NULL;
		return 0;
	}
	
	while(current != NULL){
		if (current->next != NULL){
			/* There's another element, so free() this one and
			    then move on to the next */
			next = current->next;
			if (DATA_VERBOSE){
				printf("%s.%d\t removeGamedata() Freeing gamedata list object [%s]\n", __FILE__, __LINE__, current->next->name);	
			}
			free(current);
			current = next;
		} else {
			/* There are no more elements linked */
			free(current);
			current = NULL;
			return 0;
		}
	}
	return 0;
}

int sortGamedata(gamedata_t *gamedata, int verbose){
	// Sort the list of game data objects by name
	// This is bubble sort, so it's reasonably slow, but 
	// simple to implement.
	
	int swapped;
	int compare;
	
	gamedata_t *gdata1 = NULL;
	gamedata_t *gdata2 = NULL;
	
	/* Nothing more after this point, consider it sorted */
	if (gamedata->next == NULL){
		return 0;
	}
	
	do {
		swapped = 0;
		gdata1 = gamedata;
		while (gdata1->next != gdata2){
			compare = strcmp(gdata1->name, gdata1->next->name);
			if (compare > 0){
				/* swap objects */
				swapGamedata(gdata1, gdata1->next);
				swapped = 1;
			}
			gdata1 = gdata1->next;
		}
		gdata2 = gdata1;
	}
	while (swapped);
	return 0;
}

int swapGamedata(gamedata_t *gamedata1, gamedata_t *gamedata2){
	/* swap one game object with another */
	
	gamedata_t *gdata_temp = NULL;
	gdata_temp = (gamedata_t *) calloc(1,sizeof(gamedata_t));
	
	/* keep temp store of gamedata1 */
	gdata_temp->gameid = gamedata1->gameid;
	gdata_temp->drive = gamedata1->drive;
	strcpy(gdata_temp->path, gamedata1->path);
	strcpy(gdata_temp->name, gamedata1->name);
	gdata_temp->has_dat = gamedata1->has_dat;
	
	/* swap a with b */
	gamedata1->gameid = gamedata2->gameid;
	gamedata1->drive = gamedata2->drive;
	strcpy(gamedata1->path, gamedata2->path);
	strcpy(gamedata1->name, gamedata2->name);
	gamedata1->has_dat = gamedata2->has_dat;
	
	/* swap b with temp */
	gamedata2->gameid = gdata_temp->gameid;
	gamedata2->drive = gdata_temp->drive;
	strcpy(gamedata2->path, gdata_temp->path);
	strcpy(gamedata2->name, gdata_temp->name);
	gamedata2->has_dat = gdata_temp->has_dat;
	
	/* Free up temp store */
	free(gdata_temp);
	return 0;
}

static int launchdatHandler(void* user, const char* section, const char* name, const char* value){
	/* Based on reference implementation of inih parser:
	    https://github.com/benhoyt/inih
	    */
	    
	launchdat_t* launchdat = (launchdat_t*)user;
		
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("default", "name")){
		strncpy(launchdat->realname, value, MAX_NAME_SIZE);
		
	} else if (MATCH("default", "genre")){
		strncpy(launchdat->genre, value, MAX_STRING_SIZE);
		
	} else if (MATCH("default", "developer")){
		strncpy(launchdat->developer, value, MAX_STRING_SIZE);
		
	} else if (MATCH("default", "publisher")){
		strncpy(launchdat->publisher, value, MAX_STRING_SIZE);
		
	} else if (MATCH("default", "year")){
		launchdat->year = atoi(value);
		
	} else if (MATCH("default", "midi_mpu")){
		if (atoi(value) == 1){
			launchdat->midi = 1;
		}
	} else if (MATCH("default", "midi_serial")){
		if (atoi(value) == 1){
			launchdat->midi_serial = 1;
		}
	} else if (MATCH("default", "start")){
		/* Parse comma-separated list of entries, each optionally with a label:
		   file.exe[Start Game],setup.exe[Setup],other.bat
		   Legacy single value also supported. */
		{
			char buf[IMAGE_BUFFER_SIZE];
			char *token;
			char *bracket;
			char *endbracket;
			int idx;
			
			/* Copy raw value into start for backwards compatibility */
			strncpy(launchdat->start, value, MAX_FILENAME_SIZE - 1);
			launchdat->start[MAX_FILENAME_SIZE - 1] = '\0';
			
			strncpy(buf, value, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			
			idx = 0;
			token = strtok(buf, ",");
			while (token != NULL && idx < MAX_START_ENTRIES){
				/* Skip leading whitespace */
				while (*token == ' ') token++;
				
				/* Look for optional [Label] */
				bracket = strchr(token, '[');
				if (bracket != NULL){
					endbracket = strchr(bracket, ']');
					/* Extract filename (everything before '[') */
					*bracket = '\0';
					strncpy(launchdat->start_entries[idx].file, token, MAX_FILENAME_SIZE - 1);
					launchdat->start_entries[idx].file[MAX_FILENAME_SIZE - 1] = '\0';
					/* Extract label (between '[' and ']') */
					if (endbracket != NULL){
						*endbracket = '\0';
					}
					strncpy(launchdat->start_entries[idx].label, bracket + 1, MAX_START_LABEL_SIZE - 1);
					launchdat->start_entries[idx].label[MAX_START_LABEL_SIZE - 1] = '\0';
				} else {
					/* No label - just a filename */
					strncpy(launchdat->start_entries[idx].file, token, MAX_FILENAME_SIZE - 1);
					launchdat->start_entries[idx].file[MAX_FILENAME_SIZE - 1] = '\0';
					launchdat->start_entries[idx].label[0] = '\0';
				}
				idx++;
				token = strtok(NULL, ",");
			}
			launchdat->start_count = idx;
		}
	
	} else if (MATCH("default", "alt_start")){
		/* Parse optional [Label] suffix, same as start= entries:
		   alt_start=setup.exe[Setup Game]  or just  alt_start=setup.exe */
		{
			char alt_buf[MAX_FILENAME_SIZE + MAX_START_LABEL_SIZE];
			char alt_file[MAX_FILENAME_SIZE];
			char alt_label[MAX_START_LABEL_SIZE];
			char *bracket;
			char *endbracket;
			
			strncpy(alt_buf, value, sizeof(alt_buf) - 1);
			alt_buf[sizeof(alt_buf) - 1] = '\0';
			
			bracket = strchr(alt_buf, '[');
			if (bracket != NULL){
				endbracket = strchr(bracket, ']');
				*bracket = '\0';
				strncpy(alt_file, alt_buf, MAX_FILENAME_SIZE - 1);
				alt_file[MAX_FILENAME_SIZE - 1] = '\0';
				if (endbracket != NULL) *endbracket = '\0';
				strncpy(alt_label, bracket + 1, MAX_START_LABEL_SIZE - 1);
				alt_label[MAX_START_LABEL_SIZE - 1] = '\0';
			} else {
				strncpy(alt_file, alt_buf, MAX_FILENAME_SIZE - 1);
				alt_file[MAX_FILENAME_SIZE - 1] = '\0';
				alt_label[0] = '\0';
			}
			
			/* Store bare filename into legacy alt_start field */
			strncpy(launchdat->alt_start, alt_file, MAX_FILENAME_SIZE - 1);
			launchdat->alt_start[MAX_FILENAME_SIZE - 1] = '\0';
			
			/* Add to start_entries if not already captured via start= */
			if (launchdat->start_count == 0 && strlen(alt_file) > 0){
				/* start= was not set, treat alt_start as the only entry */
				strncpy(launchdat->start_entries[0].file, alt_file, MAX_FILENAME_SIZE - 1);
				launchdat->start_entries[0].file[MAX_FILENAME_SIZE - 1] = '\0';
				strncpy(launchdat->start_entries[0].label, alt_label, MAX_START_LABEL_SIZE - 1);
				launchdat->start_entries[0].label[MAX_START_LABEL_SIZE - 1] = '\0';
				launchdat->start_count = 1;
			} else if (launchdat->start_count > 0 && launchdat->start_count < MAX_START_ENTRIES && strlen(alt_file) > 0){
				/* Append alt_start as an additional entry if not already in the list */
				int already_present = 0;
				int i;
				for (i = 0; i < launchdat->start_count; i++){
					if (strcmp(launchdat->start_entries[i].file, alt_file) == 0){
						already_present = 1;
						break;
					}
				}
				if (!already_present){
					strncpy(launchdat->start_entries[launchdat->start_count].file, alt_file, MAX_FILENAME_SIZE - 1);
					launchdat->start_entries[launchdat->start_count].file[MAX_FILENAME_SIZE - 1] = '\0';
					strncpy(launchdat->start_entries[launchdat->start_count].label, alt_label, MAX_START_LABEL_SIZE - 1);
					launchdat->start_entries[launchdat->start_count].label[MAX_START_LABEL_SIZE - 1] = '\0';
					launchdat->start_count++;
				}
			}
		}
		
	} else if (MATCH("default", "images")){
		strncpy(launchdat->images, value, IMAGE_BUFFER_SIZE);
		
	} else if (MATCH("default", "video")){
		strncpy(launchdat->video, value, MAX_FILENAME_SIZE);

	} else if (MATCH("default", "audio")){
    	strncpy(launchdat->audio, value, MAX_FILENAME_SIZE);

	} else if (MATCH("default", "series")){
		strncpy(launchdat->series, value, MAX_STRING_SIZE);
	// audio hardware metadata 
	} else if (MATCH("sound", "beeper")){
		if (atoi(value) == 1){
			launchdat->hardware->beeper = 1;
		}
	} else if (MATCH("sound", "tandy")){
		if (atoi(value) == 1){
			launchdat->hardware->tandy_audio = 1;
		}
	} else if (MATCH("sound", "adlib")){
		if (atoi(value) == 1){
			launchdat->hardware->adlib = 1;
		}
	} else if (MATCH("sound", "soundblaster")){
		if (atoi(value) == 1){
			launchdat->hardware->soundblaster = 1;
		}
	} else if (MATCH("sound", "mt32")){
		if (atoi(value) == 1){
			launchdat->hardware->mt32 = 1;
		}
	} else if (MATCH("sound", "gm")){
		if (atoi(value) == 1){
			launchdat->hardware->gm = 1;
		}
	} else if (MATCH("sound", "covox")){
		if (atoi(value) == 1){
			launchdat->hardware->covox = 1;
		}
	} else if (MATCH("sound", "disney")){
		if (atoi(value) == 1){
			launchdat->hardware->disney = 1;
		}
	} else if (MATCH("sound", "ultrasound")){
		if (atoi(value) == 1){
			launchdat->hardware->ultrasound = 1;
		}
	// video hardware metadata
	} else if (MATCH("video", "text")){
		if (atoi(value) == 1){
			launchdat->hardware->text = 1;
		}
	} else if (MATCH("video", "hercules")){
		if (atoi(value) == 1){
			launchdat->hardware->hercules = 1;
		}
	} else if (MATCH("video", "tandy")){
		if (atoi(value) == 1){
			launchdat->hardware->tandy_video = 1;
		}
	} else if (MATCH("video", "cga")){
		if (atoi(value) == 1){
			launchdat->hardware->cga = 1;
		}
	} else if (MATCH("video", "ega")){
		if (atoi(value) == 1){
			launchdat->hardware->ega = 1;
		}
	} else if (MATCH("video", "vga")){
		if (atoi(value) == 1){
			launchdat->hardware->vga = 1;
		}
	} else if (MATCH("video", "svga")){
		if (atoi(value) == 1){
			launchdat->hardware->svga = 1;
		}	
	// cpu/misc hardware metadata
	} else if (MATCH("cpu", "8086")){
		if (atoi(value) == 1){
			launchdat->hardware->cpu_8086 = 1;
		}
	} else if (MATCH("cpu", "286")){
		if (atoi(value) == 1){
			launchdat->hardware->cpu_286 = 1;
		}
	} else if (MATCH("cpu", "386")){
		if (atoi(value) == 1){
			launchdat->hardware->cpu_386 = 1;
		}
	} else if (MATCH("cpu", "486")){
		if (atoi(value) == 1){
			launchdat->hardware->cpu_486 = 1;
		}
	} else if (MATCH("cpu", "586")){
		if (atoi(value) == 1){
			launchdat->hardware->cpu_586 = 1;
		}
	} else if (MATCH("cpu", "xms")){
		if (atoi(value) == 1){
			launchdat->hardware->ram_xms = 1;
		}
	} else if (MATCH("cpu", "ems")){
		if (atoi(value) == 1){
			launchdat->hardware->ram_ems = 1;
		}
	} else if (MATCH("cpu", "dpmi")){
		if (atoi(value) == 1){
			launchdat->hardware->dpmi = 1;
		}
	} else {
		return 0;  /* unknown section/name, error */
	}
	return 1;
}

void launchdataDefaults(launchdat_t *launchdat){
	/* Set some defaults, in case various lines arent there */
	
	memset(launchdat->realname, '\0', strlen(launchdat->realname));
	memset(launchdat->genre, '\0', strlen(launchdat->genre));
	memset(launchdat->publisher, '\0', strlen(launchdat->publisher));
	memset(launchdat->developer, '\0', strlen(launchdat->developer));
	memset(launchdat->start, '\0', strlen(launchdat->start));
	memset(launchdat->alt_start, '\0', strlen(launchdat->alt_start));
	memset(launchdat->start_entries, '\0', sizeof(launchdat->start_entries));
	launchdat->start_count = 0;
	memset(launchdat->images, '\0', strlen(launchdat->images));
	memset(launchdat->video,  '\0', MAX_FILENAME_SIZE);
	memset(launchdat->audio, '\0', MAX_FILENAME_SIZE);
	memset(launchdat->series, '\0', strlen(launchdat->series));
	launchdat->year = DEFAULT_YEAR;
	launchdat->midi = 0;
	launchdat->midi_serial = 0;
	
	// Audio defaults
	launchdat->hardware->beeper = 0;
	launchdat->hardware->tandy_audio = 0;
	launchdat->hardware->adlib = 0;
	launchdat->hardware->soundblaster = 0;
	launchdat->hardware->mt32 = 0;
	launchdat->hardware->gm = 0;
	launchdat->hardware->covox = 0;
	launchdat->hardware->disney = 0;
	launchdat->hardware->ultrasound = 0;
	
	// Video defaults
	launchdat->hardware->text = 0;
	launchdat->hardware->hercules = 0;
	launchdat->hardware->tandy_video = 0;
	launchdat->hardware->cga = 0;
	launchdat->hardware->ega = 0;
	launchdat->hardware->vga = 0;
	launchdat->hardware->svga = 0;
	
	// CPU/misc defaults
	launchdat->hardware->cpu_8086 = 0;
	launchdat->hardware->cpu_286 = 0;
	launchdat->hardware->cpu_386 = 0;
	launchdat->hardware->cpu_486 = 0;
	launchdat->hardware->cpu_586 = 0;
	launchdat->hardware->ram_xms = 0;
	launchdat->hardware->ram_ems = 0;
	launchdat->hardware->dpmi = 0;
}

void configDefaults(config_t *config){
	/* Set some defaults, in case various lines arent there */
	
	config->verbose = 0;
	memset(config->dirs, '\0', strlen(config->dirs));
	config->save = 0;
	config->preload_names = 0;
	config->dir = NULL;
	config->keyboard_test = 0;
	config->hsync_shift = 0;
	config->audio_volume = 256;
}

int getLaunchdata(gamedata_t *gamedata, launchdat_t *launchdat){
	/* load and return a launch.dat from from disk, for a given gamedata object */
	
	char filepath[MAX_PATH_SIZE];
	
	if (gamedata->has_dat != 1){
		return -1;
	}
	
	strcpy(filepath, gamedata->path);
	strcat(filepath, "\\");
	strcat(filepath, GAMEDAT);
	
	launchdataDefaults(launchdat);
	if (ini_parse(filepath, launchdatHandler, launchdat) < 0) {
		if (DATA_VERBOSE){
			printf("%s.%d\t getLaunchdata() Cannot load %s\n", __FILE__, __LINE__, filepath);
		}
		return -1;
	} else {
		if (DATA_VERBOSE){
			printf("%s.%d\t getLaunchdata() Loaded %s\n", __FILE__, __LINE__, filepath);
		}
		return 0;
	}
}

static int configHandler(void* user, const char* section, const char* name, const char* value){
	/* Based on reference implementation of inih parser:
	    https://github.com/benhoyt/inih
	    */
	    
	config_t* config = (config_t*)user;
		
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("default", "verbose")){
		config->verbose =  atoi(value);
	} else if (MATCH("default", "gamedirs")){
		strncpy(config->dirs, value, MAX_SEARCHDIRS_SIZE);
	} else if (MATCH("default", "savedirs")){
		config->save =  atoi(value);
	} else if (MATCH("default", "preload_names")){
		config->preload_names =  atoi(value);
	} else if (MATCH("default", "keyboard_test")){
		config->keyboard_test =  atoi(value);
	} else if (MATCH("default", "timers")){
		config->timers =  atoi(value);
	} else if (MATCH("display", "hsync_shift")){
    config->hsync_shift = atoi(value);
	} else if (MATCH("sound", "volume")){
    config->audio_volume = atoi(value);
    if (config->audio_volume < 0)   config->audio_volume = 0;
    if (config->audio_volume > 256) config->audio_volume = 256;
	} else {
		return 0;  /* unknown section/name, error */
	}
	return 1;
}

int getIni(config_t *config, int verbose){
	/* Load the main ini file for the application */
	
	int status;
	char my_drive;
	char my_drive_letter;
	char my_dir[DIR_BUFFER_SIZE];
	char my_path[DIR_BUFFER_SIZE];
	
	if (DATA_VERBOSE){
		printf("%s.%d\t getIni() Initialising buffers\n", __FILE__, __LINE__);
	}
	memset(my_dir, '\0', sizeof(my_dir));
	memset(my_path, '\0', sizeof(my_path));
	
	getcwd(my_dir, DIR_BUFFER_SIZE);
	my_drive = drvLetterFromPath(my_dir);
	status = dirFromPath(my_dir, my_path);
	
	if (DATA_VERBOSE){
		printf("%s.%d\t getIni() Current dir: %s\n", __FILE__, __LINE__, my_dir);
	}
	
	strcpy(my_path, my_dir);
	strcat(my_path, "\\");
	strcat(my_path, INIFILE);
	
	configDefaults(config);
	
	// Back to original dir
	chdir(my_dir);
	
	if (DATA_VERBOSE){
		printf("%s.%d\t getIni() Calling parser\n", __FILE__, __LINE__);
	}
	if (ini_parse(my_path, configHandler, config) < 0) {
		if (DATA_VERBOSE){
			printf("%s.%d\t getIni() Cannot load %s\n", __FILE__, __LINE__, my_path);
		}
		return -1;
	} else {
		return 0;
	}
}

int getImageList(launchdat_t *launchdat, imagefile_t *imagefile){
	/* build a list of images as defined in launch.dat */
	/* Should only ever be called ONCE for a given imagefile!!! */
	
	char *p;
	char buffer[IMAGE_BUFFER_SIZE];
	unsigned char found;	// Counter for number of found images
	found = 0;
	
	if (DATA_VERBOSE){
		printf("%s.%d\t getImageList() Extracting image filenames for %s\n", __FILE__, __LINE__, launchdat->realname);
		printf("%s.%d\t getImageList() Images=%s\n", __FILE__, __LINE__, launchdat->images);
	}
	
	// Reset the imagefile list array
	for(found =0; found < MAX_IMAGES; found++){
		memset(imagefile->filename[found], '\0', MAX_STRING_SIZE);
	}
	found = 0;
	imagefile->selected = -1;
	imagefile->first = -1;
	imagefile->last = -1;
	imagefile->has_video = 0;
	imagefile->has_audio = 0;

	memset(imagefile->video_filename, '\0', MAX_FILENAME_SIZE);
	if (launchdat->video != NULL && strlen(launchdat->video) > 0) {
		strncpy(imagefile->video_filename, launchdat->video, MAX_FILENAME_SIZE);
			imagefile->has_video = 1;
	}	

	
	memset(imagefile->audio_filename, '\0', MAX_FILENAME_SIZE);
	if (strlen(launchdat->audio) > 0) {
		strncpy(imagefile->audio_filename, launchdat->audio, MAX_FILENAME_SIZE);
		imagefile->has_audio = 1;
	}

	if (launchdat->images != NULL){
		strncpy(buffer, launchdat->images, IMAGE_BUFFER_SIZE);
		p = strtok(buffer, ",; ");
		while (p != NULL){
			if (DATA_VERBOSE){
				printf("%s.%d\t getImageList() Extracted image filename [%s], %d len\n", __FILE__, __LINE__, p, strlen(p));
			}
			strncpy(imagefile->filename[found], p,  MAX_FILENAME_SIZE);
			
			if (found >= MAX_IMAGES){
				if (DATA_VERBOSE){
					printf("%s.%d\t getImageList() Hit limit of %d image filenames\n", __FILE__, __LINE__, MAX_IMAGES);
				}
				imagefile->selected = imagefile->has_video ? -1 : 0;
				imagefile->first    = imagefile->has_video ? -1 : 0;
				imagefile->last = found;
				return found;
			}
			imagefile->selected = imagefile->has_video ? -1 : 0;
			imagefile->first    = imagefile->has_video ? -1 : 0;
			imagefile->last = found;
			found++;
			p = strtok(NULL, ",; ");
		}
		if (DATA_VERBOSE){
			printf("%s.%d\t getImageList() Found %d image filenames\n", __FILE__, __LINE__, found);
		}
		return found;
	} else {
		return -1;	
	}	
}

int getDirList(config_t *config, gamedir_t *gamedir, int verbose){
	/* build a list of game search directoes as defined in launcher.ini */
	/* Should only ever be called ONCE at startup!!! */
	
	char *p;
	char buffer[1024];
	int found;	// Counter for number of found game search directories
	found = 0;
	
	if (config->dirs != NULL){
		strcpy(buffer, config->dirs);
		p = strtok(buffer, ",");
		while (p != NULL){
			if (isDir(p)){
				if (DATA_VERBOSE){
					printf("%s.%d\t getDirList() Game search path %s\n", __FILE__, __LINE__, p);	
				}
				found++;
				gamedir = getLastGameDir(gamedir);
				gamedir->next = (gamedir_t *) calloc(1,sizeof(gamedir_t));
				strcpy(gamedir->next->path, p);
				gamedir->next->next = NULL;
				
				// If this was the first gamedir found, add it as the head item to the config data object
				if (found == 1){
					config->dir = gamedir;	
				}
				if (found >= MAX_DIRS){
					if (DATA_VERBOSE){
						printf("%s.%d\t getDirList() Hit game search path limit [%d]\n", __FILE__, __LINE__, MAX_IMAGES);	
					}
					break;
				}
			} else {
				if (DATA_VERBOSE){
					printf("%s.%d\t getDirList() Game search path %s not found, skipping\n", __FILE__, __LINE__, p);
				}
			}
			p = strtok(NULL, ",");
		}
		if (DATA_VERBOSE){
			printf("%s.%d\t getDirList() Found [%d] game search paths\n", __FILE__, __LINE__, found);	
		}
	} else {
		if (DATA_VERBOSE){
			printf("%s.%d\t getDirList() No game search paths found\n", __FILE__, __LINE__);	
		}
		return -1;	
	}	
	return found;
}

int saveLaunchdat(gamedata_t *gamedata, launchdat_t *launchdat) {
	/* Write a launchdat_t back to the game's launch.dat file.
	   The file is rewritten in full.  Fields that the editor does
	   not touch (start=, images=, hardware flags, etc.) are
	   preserved verbatim from whatever is currently in the struct,
	   so no data is lost. */
 
	FILE *f;
	char filepath[MAX_PATH_SIZE];
	int  i;
 
	snprintf(filepath, sizeof(filepath), "%s\\%s", gamedata->path, GAMEDAT);
 
	if (DATA_VERBOSE) {
		printf("%s.%d\t saveLaunchdat() Writing %s\n", __FILE__, __LINE__, filepath);
	}
 
	f = fopen(filepath, "w");
	if (f == NULL) {
		if (DATA_VERBOSE) {
			printf("%s.%d\t saveLaunchdat() Cannot open %s for writing\n", __FILE__, __LINE__, filepath);
		}
		return -1;
	}
 
	/* ---- [default] section ---- */
	fprintf(f, "[default]\n");
 
	if (strlen(launchdat->realname) > 0) {
		fprintf(f, "name=%s\n", launchdat->realname);
	}
	if (strlen(launchdat->genre) > 0) {
		fprintf(f, "genre=%s\n", launchdat->genre);
	}
	if (strlen(launchdat->series) > 0) {
		fprintf(f, "series=%s\n", launchdat->series);
	}
	if (strlen(launchdat->developer) > 0) {
		fprintf(f, "developer=%s\n", launchdat->developer);
	}
	if (strlen(launchdat->publisher) > 0) {
		fprintf(f, "publisher=%s\n", launchdat->publisher);
	}
	if (launchdat->year > 0) {
		fprintf(f, "year=%d\n", launchdat->year);
	}
	if (launchdat->midi) {
		fprintf(f, "midi_mpu=1\n");
	}
	if (launchdat->midi_serial) {
		fprintf(f, "midi_serial=1\n");
	}
 
	/* start= entries: write in the comma-separated [Label] format so
	   a subsequent parse round-trips cleanly. */
	if (launchdat->start_count > 0) {
		fprintf(f, "start=");
		for (i = 0; i < launchdat->start_count; i++) {
			if (i > 0) {
				fprintf(f, ",");
			}
			if (strlen(launchdat->start_entries[i].label) > 0) {
				fprintf(f, "%s[%s]",
					launchdat->start_entries[i].file,
					launchdat->start_entries[i].label);
			} else {
				fprintf(f, "%s", launchdat->start_entries[i].file);
			}
		}
		fprintf(f, "\n");
	} else if (strlen(launchdat->start) > 0) {
		/* Legacy single-entry fallback */
		fprintf(f, "start=%s\n", launchdat->start);
		if (strlen(launchdat->alt_start) > 0) {
			fprintf(f, "alt_start=%s\n", launchdat->alt_start);
		}
	}
 
	if (strlen(launchdat->images) > 0) {
		fprintf(f, "images=%s\n", launchdat->images);
	}
	if (strlen(launchdat->video) > 0) {
		fprintf(f, "video=%s\n", launchdat->video);
	}
	if (strlen(launchdat->audio) > 0) {
		fprintf(f, "audio=%s\n", launchdat->audio);
	}
 
	/* ---- [sound] section - only emit if any flag is set ---- */
	if (launchdat->hardware->beeper      || launchdat->hardware->tandy_audio ||
	    launchdat->hardware->adlib       || launchdat->hardware->soundblaster ||
	    launchdat->hardware->mt32        || launchdat->hardware->gm ||
	    launchdat->hardware->covox       || launchdat->hardware->disney ||
	    launchdat->hardware->ultrasound) {
 
		fprintf(f, "\n[sound]\n");
		if (launchdat->hardware->beeper)      fprintf(f, "beeper=1\n");
		if (launchdat->hardware->tandy_audio) fprintf(f, "tandy=1\n");
		if (launchdat->hardware->adlib)       fprintf(f, "adlib=1\n");
		if (launchdat->hardware->soundblaster)fprintf(f, "soundblaster=1\n");
		if (launchdat->hardware->mt32)        fprintf(f, "mt32=1\n");
		if (launchdat->hardware->gm)          fprintf(f, "gm=1\n");
		if (launchdat->hardware->covox)       fprintf(f, "covox=1\n");
		if (launchdat->hardware->disney)      fprintf(f, "disney=1\n");
		if (launchdat->hardware->ultrasound)  fprintf(f, "ultrasound=1\n");
	}
 
	/* ---- [video] section ---- */
	if (launchdat->hardware->text     || launchdat->hardware->hercules ||
	    launchdat->hardware->tandy_video || launchdat->hardware->cga  ||
	    launchdat->hardware->ega      || launchdat->hardware->vga     ||
	    launchdat->hardware->svga) {
 
		fprintf(f, "\n[video]\n");
		if (launchdat->hardware->text)       fprintf(f, "text=1\n");
		if (launchdat->hardware->hercules)   fprintf(f, "hercules=1\n");
		if (launchdat->hardware->tandy_video)fprintf(f, "tandy=1\n");
		if (launchdat->hardware->cga)        fprintf(f, "cga=1\n");
		if (launchdat->hardware->ega)        fprintf(f, "ega=1\n");
		if (launchdat->hardware->vga)        fprintf(f, "vga=1\n");
		if (launchdat->hardware->svga)       fprintf(f, "svga=1\n");
	}
 
	/* ---- [cpu] section ---- */
	if (launchdat->hardware->cpu_8086 || launchdat->hardware->cpu_286 ||
	    launchdat->hardware->cpu_386  || launchdat->hardware->cpu_486 ||
	    launchdat->hardware->cpu_586  || launchdat->hardware->ram_xms ||
	    launchdat->hardware->ram_ems  || launchdat->hardware->dpmi) {
 
		fprintf(f, "\n[cpu]\n");
		if (launchdat->hardware->cpu_8086) fprintf(f, "8086=1\n");
		if (launchdat->hardware->cpu_286)  fprintf(f, "286=1\n");
		if (launchdat->hardware->cpu_386)  fprintf(f, "386=1\n");
		if (launchdat->hardware->cpu_486)  fprintf(f, "486=1\n");
		if (launchdat->hardware->cpu_586)  fprintf(f, "586=1\n");
		if (launchdat->hardware->ram_xms)  fprintf(f, "xms=1\n");
		if (launchdat->hardware->ram_ems)  fprintf(f, "ems=1\n");
		if (launchdat->hardware->dpmi)     fprintf(f, "dpmi=1\n");
	}
 
	fclose(f);
 
	if (DATA_VERBOSE) {
		printf("%s.%d\t saveLaunchdat() Wrote %s OK\n", __FILE__, __LINE__, filepath);
	}
	return 0;
}