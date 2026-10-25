/* NGP LINK (NelfePlay, 25/10) : deux Neo Geo Pocket reliees par le cable de liaison, dans un seul coeur libretro.
 *
 * Le coeur charge deux copies du moteur (ngp_link_engine_a.dll, ngp_link_engine_b.dll : Beetle NGP compile avec
 * NGPLINK, chaque copie a ses propres variables), relie leurs ports serie par deux files, et montre les deux ecrans
 * cote a cote. La manette 1 joue sur la console 1, la manette 2 sur la console 2 : en netplay, chaque borne a sa
 * console. Les deux consoles tournent tour a tour, une image chacune, dans le meme retro_run : tout est deterministe,
 * la sauvegarde d'etat porte les deux consoles ET les octets en route sur le cable. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "libretro.h"

#define LARGEUR_ECRAN 160
#define HAUTEUR_ECRAN 152
#define CAPACITE_FILE 4096

typedef struct
{
   HMODULE dll;
   void (*set_environment)(retro_environment_t);
   void (*set_video_refresh)(retro_video_refresh_t);
   void (*set_audio_sample)(retro_audio_sample_t);
   void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*set_input_poll)(retro_input_poll_t);
   void (*set_input_state)(retro_input_state_t);
   void (*init)(void);
   void (*deinit)(void);
   void (*get_system_info)(struct retro_system_info *);
   void (*get_system_av_info)(struct retro_system_av_info *);
   void (*set_controller_port_device)(unsigned, unsigned);
   void (*reset)(void);
   void (*run)(void);
   size_t (*serialize_size)(void);
   bool (*serialize)(void *, size_t);
   bool (*unserialize)(const void *, size_t);
   void (*cheat_reset)(void);
   void (*cheat_set)(unsigned, bool, const char *);
   bool (*load_game)(const struct retro_game_info *);
   void (*unload_game)(void);
   unsigned (*get_region)(void);
   void *(*get_memory_data)(unsigned);
   size_t (*get_memory_size)(unsigned);
   void (*set_comms)(bool (*)(uint8_t *), bool (*)(uint8_t *), void (*)(uint8_t));
   void (*set_flash_suffix)(const char *);
   uint16_t image[LARGEUR_ECRAN * HAUTEUR_ECRAN];
} moteur_t;

/* Une file d'octets du cable : ce qu'une console a ecrit et que l'autre n'a pas encore lu. */
typedef struct
{
   uint32_t debut;
   uint32_t nombre;
   uint8_t octets[CAPACITE_FILE];
} file_t;

static moteur_t consoles[2];
static file_t cable[2]; /* cable[0] : de la console 1 vers la 2 ; cable[1] : de la 2 vers la 1 */
static bool charges;
static char erreur[512];

static retro_environment_t env_front;
static retro_video_refresh_t video_front;
static retro_audio_sample_t audio_front;
static retro_audio_sample_batch_t audio_batch_front;
static retro_input_poll_t poll_front;
static retro_input_state_t state_front;
static bool mise_a_jour_b;
static uint16_t ecran[LARGEUR_ECRAN * 2 * HAUTEUR_ECRAN];

static void pousser(file_t *f, uint8_t octet)
{
   if (f->nombre >= CAPACITE_FILE)
      return; /* le cable deborde : l'octet est perdu, comme un tampon plein */
   f->octets[(f->debut + f->nombre) % CAPACITE_FILE] = octet;
   f->nombre++;
}

static bool regarder(file_t *f, uint8_t *octet, bool consommer)
{
   if (f->nombre == 0)
      return false;
   if (octet)
   {
      *octet = f->octets[f->debut];
      if (consommer)
      {
         f->debut = (f->debut + 1) % CAPACITE_FILE;
         f->nombre--;
      }
   }
   return true;
}

/* system_comms_read : sans tampon, seulement l'etat ; avec, l'octet est consomme. system_comms_poll : regarde sans
 * consommer (l'interruption de reception le depose, le jeu le lit ensuite par le BIOS). */
/* Compteurs de mise au point : appels de lecture, de guet et d'ecriture de chaque console. */
static unsigned compte[2][3];
static bool lire_a(uint8_t *o) { compte[0][0]++; return regarder(&cable[1], o, true); }
static bool guetter_a(uint8_t *o) { compte[0][1]++; return regarder(&cable[1], o, false); }
static void ecrire_a(uint8_t o) { compte[0][2]++; pousser(&cable[0], o); }
static bool lire_b(uint8_t *o) { compte[1][0]++; return regarder(&cable[0], o, true); }
static bool guetter_b(uint8_t *o) { compte[1][1]++; return regarder(&cable[0], o, false); }
static void ecrire_b(uint8_t o) { compte[1][2]++; pousser(&cable[1], o); }

static void journal(const char *texte)
{
   struct retro_log_callback log;
   if (env_front && env_front(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log) && log.log)
      log.log(RETRO_LOG_INFO, "[NGP Link] %s\n", texte);
}

/* Traces de mise au point : chaque appel du frontal ou des moteurs apres les premieres images. */
static unsigned images_jouees;
#ifdef NGPLINK_TRACES
#define TRACE(...) do { if (images_jouees >= 2 && images_jouees < 4000) { char t_[200]; snprintf(t_, sizeof(t_), __VA_ARGS__); journal(t_); } } while (0)
#else
#define TRACE(...) do { } while (0)
#endif

static bool charger_moteur(moteur_t *m, const wchar_t *chemin)
{
   m->dll = LoadLibraryW(chemin);
   if (!m->dll)
   {
      snprintf(erreur, sizeof(erreur), "moteur introuvable (%lu)", (unsigned long)GetLastError());
      return false;
   }
#define PRENDRE(champ, nom) \
   if (!(*(FARPROC *)&m->champ = GetProcAddress(m->dll, nom))) \
   { \
      snprintf(erreur, sizeof(erreur), "fonction absente du moteur : %s", nom); \
      return false; \
   }
   PRENDRE(set_environment, "retro_set_environment")
   PRENDRE(set_video_refresh, "retro_set_video_refresh")
   PRENDRE(set_audio_sample, "retro_set_audio_sample")
   PRENDRE(set_audio_sample_batch, "retro_set_audio_sample_batch")
   PRENDRE(set_input_poll, "retro_set_input_poll")
   PRENDRE(set_input_state, "retro_set_input_state")
   PRENDRE(init, "retro_init")
   PRENDRE(deinit, "retro_deinit")
   PRENDRE(get_system_info, "retro_get_system_info")
   PRENDRE(get_system_av_info, "retro_get_system_av_info")
   PRENDRE(set_controller_port_device, "retro_set_controller_port_device")
   PRENDRE(reset, "retro_reset")
   PRENDRE(run, "retro_run")
   PRENDRE(serialize_size, "retro_serialize_size")
   PRENDRE(serialize, "retro_serialize")
   PRENDRE(unserialize, "retro_unserialize")
   PRENDRE(cheat_reset, "retro_cheat_reset")
   PRENDRE(cheat_set, "retro_cheat_set")
   PRENDRE(load_game, "retro_load_game")
   PRENDRE(unload_game, "retro_unload_game")
   PRENDRE(get_region, "retro_get_region")
   PRENDRE(get_memory_data, "retro_get_memory_data")
   PRENDRE(get_memory_size, "retro_get_memory_size")
   PRENDRE(set_comms, "ngplink_set_comms")
   PRENDRE(set_flash_suffix, "ngplink_set_flash_suffix")
#undef PRENDRE
   return true;
}

/* Les deux moteurs sont a cote de ce coeur, sous deux noms : Windows ne charge qu'une fois un meme fichier, deux
 * fichiers donnent deux jeux de variables. */
static bool charger(void)
{
   HMODULE moi = NULL;
   wchar_t dossier[MAX_PATH], chemin[MAX_PATH];
   wchar_t *barre;
   if (charges)
      return true;
   if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&charger, &moi) || !GetModuleFileNameW(moi, dossier, MAX_PATH))
      return false;
   barre = wcsrchr(dossier, L'\\');
   if (barre)
      barre[1] = 0;
   _snwprintf(chemin, MAX_PATH, L"%sngp_link_engine_a.dll", dossier);
   if (!charger_moteur(&consoles[0], chemin))
      return false;
   _snwprintf(chemin, MAX_PATH, L"%sngp_link_engine_b.dll", dossier);
   if (!charger_moteur(&consoles[1], chemin))
      return false;
   consoles[0].set_comms(lire_a, guetter_a, ecrire_a);
   consoles[1].set_comms(lire_b, guetter_b, ecrire_b);
   consoles[1].set_flash_suffix("flash-p2");
   charges = true;
   return true;
}

/* L'environnement : la console 1 parle au frontal ; la console 2 lit seulement (ses declarations doubleraient
 * celles de la 1). La geometrie est celle du coeur, deux ecrans cote a cote. */
static bool env_a(unsigned cmd, void *data)
{
   if (cmd != RETRO_ENVIRONMENT_GET_LOG_INTERFACE)
      TRACE("env 1 : %u", cmd);
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      {
         bool r = env_front(cmd, data);
         if (r && data && *(bool *)data)
            mise_a_jour_b = true;
         return r;
      }
      default:
         return env_front(cmd, data);
   }
}

static bool env_b(unsigned cmd, void *data)
{
   if (cmd != RETRO_ENVIRONMENT_GET_LOG_INTERFACE)
      TRACE("env 2 : %u", cmd);
   switch (cmd)
   {
      /* Les declarations (format des pixels, options, descriptions, cartes memoire...) : deja faites par la console 1. */
      case RETRO_ENVIRONMENT_SET_ROTATION:
      case RETRO_ENVIRONMENT_SET_MESSAGE:
      case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
      case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
      case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
      case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
      case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         if (data)
            *(bool *)data = mise_a_jour_b;
         mise_a_jour_b = false;
         return true;
      default:
         /* Les questions (dossiers, options, journal, contenu charge...) : la meme reponse que pour la console 1. */
         return env_front(cmd, data);
   }
}

static void video_console(moteur_t *m, const void *donnees, unsigned largeur, unsigned hauteur, size_t pas)
{
   unsigned y;
   if (!donnees)
      return; /* image repetee : on garde la precedente */
   if (largeur > LARGEUR_ECRAN)
      largeur = LARGEUR_ECRAN;
   if (hauteur > HAUTEUR_ECRAN)
      hauteur = HAUTEUR_ECRAN;
   for (y = 0; y < hauteur; y++)
      memcpy(m->image + y * LARGEUR_ECRAN, (const uint8_t *)donnees + y * pas, largeur * 2);
}

static void video_a(const void *d, unsigned w, unsigned h, size_t p) { video_console(&consoles[0], d, w, h, p); }
static void video_b(const void *d, unsigned w, unsigned h, size_t p) { video_console(&consoles[1], d, w, h, p); }

/* Le son : celui de la console 1 (deux fois la meme musique se melangerait mal). */
static void son_a(int16_t g, int16_t d) { audio_front(g, d); }
static size_t son_lot_a(const int16_t *d, size_t n) { return audio_batch_front(d, n); }
static void son_b(int16_t g, int16_t d) { (void)g; (void)d; }
static size_t son_lot_b(const int16_t *d, size_t n) { (void)d; return n; }

static void sonder_a(void) { poll_front(); }
static void sonder_b(void) { }
static int16_t etat_a(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
   return port == 0 ? state_front(0, dev, idx, id) : 0;
}
static int16_t etat_b(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
   return port == 0 ? state_front(1, dev, idx, id) : 0;
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
   env_front = cb;
   if (!charger())
   {
      journal(erreur);
      return;
   }
   consoles[0].set_environment(env_a);
   consoles[1].set_environment(env_b);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_front = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_front = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_front = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll_front = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { state_front = cb; }

RETRO_API void retro_init(void)
{
   int i;
   if (!charger())
      return;
   for (i = 0; i < 2; i++)
   {
      consoles[i].set_video_refresh(i == 0 ? video_a : video_b);
      consoles[i].set_audio_sample(i == 0 ? son_a : son_b);
      consoles[i].set_audio_sample_batch(i == 0 ? son_lot_a : son_lot_b);
      consoles[i].set_input_poll(i == 0 ? sonder_a : sonder_b);
      consoles[i].set_input_state(i == 0 ? etat_a : etat_b);
      consoles[i].init();
   }
   memset(cable, 0, sizeof(cable));
}

RETRO_API void retro_deinit(void)
{
   if (!charges)
      return;
   consoles[0].deinit();
   consoles[1].deinit();
}

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   if (charger())
      consoles[0].get_system_info(info);
   info->library_name = "Beetle NGP Link";
   info->library_version = "0.1 NelfePlay";
   if (!info->valid_extensions)
      info->valid_extensions = "ngp|ngc|ngpc|npc";
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   consoles[0].get_system_av_info(info);
   info->geometry.base_width = LARGEUR_ECRAN * 2;
   info->geometry.base_height = HAUTEUR_ECRAN;
   info->geometry.max_width = LARGEUR_ECRAN * 2;
   info->geometry.max_height = HAUTEUR_ECRAN;
   info->geometry.aspect_ratio = (float)(LARGEUR_ECRAN * 2) / (float)HAUTEUR_ECRAN;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
   TRACE("port %u device %u", port, device);
   if (port < 2)
      consoles[port].set_controller_port_device(0, device);
}

RETRO_API void retro_reset(void)
{
   consoles[0].reset();
   consoles[1].reset();
   memset(cable, 0, sizeof(cable));
}

RETRO_API void retro_run(void)
{
   unsigned y;
   if (images_jouees % 60 == 0)
      TRACE("image %u : console 1", images_jouees);
   consoles[0].run();
   if (images_jouees % 60 == 0)
      TRACE("image %u : console 2", images_jouees);
   consoles[1].run();
   images_jouees++;
   if (images_jouees % 300 == 0)
   {
      char t[200];
      snprintf(t, sizeof(t), "cable a %u images : console 1 lit %u guette %u ecrit %u, console 2 lit %u guette %u ecrit %u, en route %u / %u",
            images_jouees, compte[0][0], compte[0][1], compte[0][2], compte[1][0], compte[1][1], compte[1][2],
            cable[0].nombre, cable[1].nombre);
      journal(t);
   }
   for (y = 0; y < HAUTEUR_ECRAN; y++)
   {
      memcpy(ecran + y * LARGEUR_ECRAN * 2, consoles[0].image + y * LARGEUR_ECRAN, LARGEUR_ECRAN * 2);
      memcpy(ecran + y * LARGEUR_ECRAN * 2 + LARGEUR_ECRAN, consoles[1].image + y * LARGEUR_ECRAN, LARGEUR_ECRAN * 2);
   }
   video_front(ecran, LARGEUR_ECRAN * 2, HAUTEUR_ECRAN, LARGEUR_ECRAN * 2 * sizeof(uint16_t));
}

/* L'etat : [taille 1][taille 2][console 1][console 2][les deux files du cable]. */
RETRO_API size_t retro_serialize_size(void)
{
   TRACE("serialize_size");
   return 2 * sizeof(uint32_t) + consoles[0].serialize_size() + consoles[1].serialize_size() + sizeof(cable);
}

RETRO_API bool retro_serialize(void *donnees, size_t taille)
{
   TRACE("serialize %u", (unsigned)taille);
   uint8_t *p = (uint8_t *)donnees;
   uint32_t t1 = (uint32_t)consoles[0].serialize_size(), t2 = (uint32_t)consoles[1].serialize_size();
   if (taille < 2 * sizeof(uint32_t) + t1 + t2 + sizeof(cable))
      return false;
   memcpy(p, &t1, sizeof(t1));
   memcpy(p + 4, &t2, sizeof(t2));
   p += 8;
   if (!consoles[0].serialize(p, t1))
      return false;
   p += t1;
   if (!consoles[1].serialize(p, t2))
      return false;
   p += t2;
   memcpy(p, cable, sizeof(cable));
   return true;
}

RETRO_API bool retro_unserialize(const void *donnees, size_t taille)
{
   TRACE("unserialize %u", (unsigned)taille);
   const uint8_t *p = (const uint8_t *)donnees;
   uint32_t t1, t2;
   if (taille < 8)
      return false;
   memcpy(&t1, p, sizeof(t1));
   memcpy(&t2, p + 4, sizeof(t2));
   if (taille < 8 + (size_t)t1 + t2 + sizeof(cable))
      return false;
   p += 8;
   if (!consoles[0].unserialize(p, t1))
      return false;
   p += t1;
   if (!consoles[1].unserialize(p, t2))
      return false;
   p += t2;
   memcpy(cable, p, sizeof(cable));
   return true;
}

RETRO_API void retro_cheat_reset(void) { consoles[0].cheat_reset(); }
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { consoles[0].cheat_set(index, enabled, code); }

RETRO_API bool retro_load_game(const struct retro_game_info *info)
{
   if (!charges)
   {
      journal(erreur);
      return false;
   }
   memset(cable, 0, sizeof(cable));
   if (!consoles[0].load_game(info))
   {
      journal("console 1 : jeu refuse");
      return false;
   }
   journal("console 1 : jeu charge");
   if (!consoles[1].load_game(info))
   {
      journal("console 2 : jeu refuse");
      return false;
   }
   journal("console 2 : jeu charge");
   return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type; (void)info; (void)num;
   return false;
}

RETRO_API void retro_unload_game(void)
{
   consoles[0].unload_game();
   consoles[1].unload_game();
}

RETRO_API unsigned retro_get_region(void) { return consoles[0].get_region(); }

/* La memoire exposee : celle de la console 1 (sauvegarde, succes). */
RETRO_API void *retro_get_memory_data(unsigned id) { TRACE("memory_data %u", id); return consoles[0].get_memory_data(id); }
RETRO_API size_t retro_get_memory_size(unsigned id) { TRACE("memory_size %u", id); return consoles[0].get_memory_size(id); }
