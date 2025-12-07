/*
 * protocol.h
 *
 * Shared header file for UDP client and server
 * Contains protocol definitions, data structures, constants and function prototypes
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>

/*
 * ============================================================================
 * PROTOCOL CONSTANTS
 * ============================================================================
 */

/* Parametri applicazione condivisi */
#define SERVER_IP "127.0.0.1"     // Indirizzo IP default del server (localhost)
#define SERVER_PORT 56700         // Porta default del server UDP (SPECIFICATA NELLA TRACCIA)
#define BUFFER_SIZE 512           // Dimensione buffer per messaggi

/* Codici di stato della risposta del server */
#define STATUS_SUCCESS 0          // Richiesta elaborata con successo
#define STATUS_CITY_NOT_FOUND 1   // Città richiesta non disponibile
#define STATUS_INVALID_REQUEST 2  // Richiesta non valida

/* Tipi di dati meteorologici supportati */
#define TYPE_TEMPERATURE 't'      // Temperatura
#define TYPE_HUMIDITY 'h'         // Umidità
#define TYPE_WIND 'w'             // Velocità del vento
#define TYPE_PRESSURE 'p'         // Pressione atmosferica

/*
 * ============================================================================
 * PROTOCOL DATA STRUCTURES
 * ============================================================================
 */

/* Struttura richiesta client (client -> server) */
typedef struct {
    char type;        // Tipo di dato meteo: 't', 'h', 'w', 'p'
    char city[64];    // Nome città (stringa null-terminated)
} weather_request_t;

/* Struttura risposta server (server -> client) */
typedef struct {
    unsigned int status;  // Codice di stato: 0=successo, 1=città non trovata, 2=richiesta invalida
    char type;            // Echo del tipo richiesto
    float value;          // Valore dato meteo generato
} weather_response_t;

/* DIMENSIONI MESSAGGI SULLA RETE */
/* Dimensione messaggio richiesta: type (1) + city (64) = 65 byte */
#define REQUEST_SIZE (sizeof(char) + 64)

/* Dimensione messaggio risposta: status (4) + type (1) + value (4) = 9 byte */
#define RESPONSE_SIZE (sizeof(uint32_t) + sizeof(char) + sizeof(float))

/*
 * ============================================================================
 * FUNCTION PROTOTYPES
 * ============================================================================
 */

/* --- Funzioni di serializzazione/deserializzazione --- */

/*
 * Serializza una richiesta nel buffer per invio sulla rete
 * Converte i campi nel formato network byte order dove necessario
 */
int serialize_request(const weather_request_t *request, uint8_t *buffer);

/*
 * Deserializza una richiesta ricevuta dalla rete
 * Converte i campi dal formato network byte order
 */
int deserialize_request(const uint8_t *buffer, weather_request_t *request);

/*
 * Serializza una risposta nel buffer per invio sulla rete
 * Converte i campi nel formato network byte order dove necessario
 */
int serialize_response(const weather_response_t *response, uint8_t *buffer);

/*
 * Deserializza una risposta ricevuta dalla rete
 * Converte i campi dal formato network byte order
 */
int deserialize_response(const uint8_t *buffer, weather_response_t *response);

/*
 * Effettua il parsing della stringa di richiesta utente (lato CLIENT)
 * Input: "type city" (es: "t roma")
 * Output: riempie la struct weather_request_t
 */
int parse_weather_request(const char *input, weather_request_t *request);

/*
 * Valida una richiesta lato SERVER
 * Verifica tipo valido e città supportata
 * Ritorna: STATUS_SUCCESS, STATUS_CITY_NOT_FOUND, o STATUS_INVALID_REQUEST
 */
int validate_request_server(const weather_request_t *request);

/* Funzioni di generazione dati meteorologici */

void initialize_random_generator(void);
float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);

/* --- Funzioni di utilità DNS --- */

/*
 * Risolve un hostname/IP ottenendo sia il nome che l'indirizzo
 * Supporta: hostname -> IP e IP -> hostname (reverse lookup)
 */
int resolve_host(const char *input, char *hostname_out, size_t hostname_size, char *ip_out, size_t ip_size);

#endif /* PROTOCOL_H_ */
