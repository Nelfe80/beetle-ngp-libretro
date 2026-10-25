#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "flash.h"
#include "system.h"
#include "rom.h"

#include "../general.h"
#include "../state.h"

#include <streams/file_stream.h>

#ifdef NGPLINK
/* NGP LINK (NelfePlay) : le port de liaison est branche par le coeur enveloppe, qui charge deux moteurs et relie
 * leurs ports. Sans branchement, le port reste muet comme dans le coeur d'origine. */
#ifndef NGPLINK_API
#ifdef _WIN32
#define NGPLINK_API __declspec(dllexport)
#else
#define NGPLINK_API __attribute__((visibility("default")))
#endif
#endif
static bool (*link_read)(uint8_t*) = NULL;
static bool (*link_poll)(uint8_t*) = NULL;
static void (*link_write)(uint8_t) = NULL;
static char flash_suffix[32] = "flash";

NGPLINK_API void ngplink_set_comms(bool (*lire)(uint8_t*), bool (*guetter)(uint8_t*), void (*ecrire)(uint8_t))
{
   link_read  = lire;
   link_poll  = guetter;
   link_write = ecrire;
}

/* La console 2 sauve sa flash a part : sinon les deux consoles ecriraient le meme fichier. */
NGPLINK_API void ngplink_set_flash_suffix(const char *suffixe)
{
   strncpy(flash_suffix, suffixe, sizeof(flash_suffix) - 1);
   flash_suffix[sizeof(flash_suffix) - 1] = 0;
}

bool system_comms_read(uint8_t* buffer) { return link_read ? link_read(buffer) : 0; }
bool system_comms_poll(uint8_t* buffer) { return link_poll ? link_poll(buffer) : 0; }
void system_comms_write(uint8_t data) { if (link_write) link_write(data); }

/* COMRECIVESTATUS rend le NOMBRE d'octets recus (le BIOS reel compte son tampon) : un jeu qui attend une trame
 * entiere avant de la lire voyait toujours « 1 ». Comme l'emulation de caioluders/ngp-link. */
static int (*link_count)(void) = NULL;
NGPLINK_API void ngplink_set_count(int (*compter)(void)) { link_count = compter; }
int system_comms_count(void) { return link_count ? link_count() : (system_comms_read(NULL) ? 1 : 0); }

/* L'interruption de reception a chaque ligne tant qu'un octet attend : les jeux « BIOS COM » (Card Fighters' Clash)
 * ne posent pas de gestionnaire et interrogent le BIOS ; seuls les jeux qui pilotent eux-memes le port serie (SNK vs
 * Capcom, KOF R-2) en ont besoin. Coupee par defaut. */
int ngplink_rx_interrupt = 0;
NGPLINK_API void ngplink_set_rx_interrupt(int actif) { ngplink_rx_interrupt = actif; }

/* La flash ecrite sur le disque, ou gardee en memoire : en netplay, une borne recoit l'etat (et donc la flash) de
 * l'autre, et ne doit pas l'ecrire par-dessus la sauvegarde de son joueur. */
static int ecrire_la_flash = 1;
NGPLINK_API void ngplink_set_flash_write(int actif) { ecrire_la_flash = actif; }
#define FLASH_SUFFIX flash_suffix
#else
bool system_comms_read(uint8_t* buffer) { return 0; }
bool system_comms_poll(uint8_t* buffer) { return 0; }
void system_comms_write(uint8_t data) { }
#define FLASH_SUFFIX "flash"
#endif

bool system_io_flash_read(uint8_t *s, uint32_t len)
{
   char path_str[1024];
   RFILE *flash_fp = NULL;

   MDFN_MakeFName(MDFNMKF_SAV, path_str, sizeof(path_str), 0, FLASH_SUFFIX);
   flash_fp = filestream_open(path_str,
         RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if (!flash_fp)
      return 0;

   filestream_read(flash_fp, s, len);
   filestream_close(flash_fp);

   return 1;
}

bool system_io_flash_write(uint8_t *s, uint32_t len)
{
   char path_str[1024];
   RFILE *flash_fp = NULL;

#ifdef NGPLINK
   if (!ecrire_la_flash)
      return 1;
#endif
   MDFN_MakeFName(MDFNMKF_SAV, path_str, sizeof(path_str), 0, FLASH_SUFFIX);
   flash_fp = filestream_open(path_str,
         RETRO_VFS_FILE_ACCESS_WRITE,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if (!flash_fp)
      return 0;

   filestream_write(flash_fp, s, len);
   filestream_close(flash_fp);

   return 1;
}

int FLASH_StateAction(void *data, int load, int data_only)
{
   int32_t FlashLength = 0;
   uint8_t *flashdata = NULL;
   SFORMAT FINF_StateRegs[] =
   {
      { &FlashLength, sizeof(FlashLength), 0x80000000, "FlashLength" },
      { 0, 0, 0, 0 }
   };
   SFORMAT FLSH_StateRegs[] =
   {
      { flashdata, (uint32_t)FlashLength, 0, "flashdata" },
      { 0, 0, 0, 0 }
   };

   if(!load)
      flashdata = make_flash_commit(&FlashLength);

   if(!MDFNSS_StateAction(data, load, data_only, FINF_StateRegs, "FINF", false))
   {
      if(flashdata)
         free(flashdata);
      return 0;
   }

   if(!FlashLength) // No flash data to save, OR no flash data to load.
   {
      if(flashdata)
         free(flashdata);
      return 1;
   }

   if(load)
      flashdata = (uint8_t *)malloc(FlashLength);

   (*FLSH_StateRegs).v = flashdata;
   (*FLSH_StateRegs).size = FlashLength;

   if(!MDFNSS_StateAction(data, load, data_only, FLSH_StateRegs, "FLSH", false))
   {
      free(flashdata);
      return 0;
   }

   if(load)
   {
      memcpy(ngpc_rom.data, ngpc_rom.orig_data, ngpc_rom.length);
      do_flash_read(flashdata);
   }

   free(flashdata);
   return 1;
}
