/*
 * main.c
 *
 * UDP Server - Weather Service
 */

#if defined WIN32
#include <winsock.h>
#include <windows.h>
typedef int socklen_t;
#else
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include "protocol.h"


void clearwinsock() {
#if defined WIN32
	WSACleanup();
#endif
}

// Stampa un messaggio di errore su stream per errori
void print_error(const char* Messaggio_di_errore){
	fprintf(stderr, "%s", Messaggio_di_errore);
}
/*
 * Generazione numeri casuali e dati meteo
 */
void initialize_random_generator(void) {
	srand((unsigned int)time(NULL));
}

static float generate_random_float(float min_val, float max_val) {
	float random_fraction = (float)rand() / (float)RAND_MAX;
	return min_val + random_fraction * (max_val - min_val);
}

float get_temperature(void) {
	return generate_random_float(-10.0f, 40.0f);
}

float get_humidity(void) {
	return generate_random_float(20.0f, 100.0f);
}

float get_wind(void) {
	return generate_random_float(0.0f, 100.0f);
}

float get_pressure(void) {
	return generate_random_float(950.0f, 1050.0f);
}

/*
 * Validazione città
 */
static int compare_case_insensitive(const char *str1, const char *str2) {
	while (*str1 && *str2) {
		if (tolower((unsigned char)*str1) != tolower((unsigned char)*str2)) {
			return 0;
		}
		str1++;
		str2++;
	}
	return *str1 == '\0' && *str2 == '\0';
}

static int check_city_availability(const char *city_name) {
	const char *supported_cities[] = {
		"Bari", "Roma", "Milano", "Napoli", "Torino",
		"Palermo", "Genova", "Bologna", "Firenze", "Venezia"
	};

	const int total_cities = (int)(sizeof(supported_cities) / sizeof(supported_cities[0]));

	for (int i = 0; i < total_cities; i++) {
		if (compare_case_insensitive(city_name, supported_cities[i])) {
			return 1;
		}
	}
	return 0;
}

/*
 * Validazione richiesta lato server
 */
int validate_request_server(const weather_request_t *request) {
	if (!request) {
		return STATUS_INVALID_REQUEST;
	}

	// Validazione tipo
	if (request->type != TYPE_TEMPERATURE &&
	    request->type != TYPE_HUMIDITY &&
	    request->type != TYPE_WIND &&
	    request->type != TYPE_PRESSURE) {
		return STATUS_INVALID_REQUEST;
	}

	// Validazione città: verifica assenza caratteri tab e speciali
	const char *city_ptr = request->city;
	while (*city_ptr) {
		if (*city_ptr == '\t') {
			return STATUS_INVALID_REQUEST;
		}
		// Caratteri ammessi: lettere, spazi, apostrofi, trattini
		if (!isalpha((unsigned char)*city_ptr) &&
		    *city_ptr != ' ' &&
		    *city_ptr != '\'' &&
		    *city_ptr != '-') {
			return STATUS_INVALID_REQUEST;
		}
		city_ptr++;
	}

	// Verifica disponibilità città
	if (!check_city_availability(request->city)) {
		return STATUS_CITY_NOT_FOUND;
	}

	return STATUS_SUCCESS;
}

/*
 * Deserializzazione richiesta
 * Wire format: [type: 1 byte] [city: 64 byte]
 */
int deserialize_request(const uint8_t *buffer, weather_request_t *request) {
	if (!buffer || !request) {
		return -1;
	}

	int offset = 0;

	// Campo type: 1 byte
	request->type = (char)buffer[offset];
	offset += 1;

	// Campo city: 64 byte
	memcpy(request->city, buffer + offset, 64);
	request->city[63] = '\0'; // Assicura null-termination
	offset += 64;

	return 0;
}

/*
 * Serializzazione risposta
 */
int serialize_response(const weather_response_t *response, uint8_t *buffer) {
	if (!response || !buffer) {
		return -1;
	}

	int offset = 0;

	// Campo status: 4 byte uint32_t - CONVERSIONE in network byte order
	uint32_t net_status = htonl(response->status);
	memcpy(buffer + offset, &net_status, sizeof(uint32_t));
	offset += sizeof(uint32_t);

	// Campo type: 1 byte - nessuna conversione
	buffer[offset] = (uint8_t)response->type;
	offset += 1;

	// Campo value: 4 byte float - CONVERSIONE in network byte order
	// Tecnica: float -> uint32_t -> htonl() -> buffer
	uint32_t bits;
	memcpy(&bits, &response->value, sizeof(float));
	uint32_t net_bits = htonl(bits);
	memcpy(buffer + offset, &net_bits, sizeof(uint32_t));
	offset += sizeof(uint32_t);

	return offset; // Ritorna 9 byte
}

/*
 * Risoluzione indirizzo client (reverse lookup)
 */
int resolve_client_address(struct in_addr *addr, char *hostname_out,
                           size_t hostname_size, char *ip_out, size_t ip_size) {
	if (!addr || !hostname_out || !ip_out) {
		return -1;
	}

	// IP come stringa
	char *ip_str = inet_ntoa(*addr);
	strncpy(ip_out, ip_str, ip_size - 1);
	ip_out[ip_size - 1] = '\0';

	// Reverse lookup: IP -> hostname
	struct hostent *host = gethostbyaddr((char *)addr, sizeof(struct in_addr), AF_INET);

	if (!host) {
		// Fallback: usa IP come hostname
		strncpy(hostname_out, ip_str, hostname_size - 1);
		hostname_out[hostname_size - 1] = '\0';
	} else {
		strncpy(hostname_out, host->h_name, hostname_size - 1);
		hostname_out[hostname_size - 1] = '\0';
	}

	return 0;
}

int main(int argc, char *argv[]) {

	// Porta di default
	int listen_port = SERVER_PORT;

	// PARSING ARGOMENTI
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0) {
			if (i + 1 < argc) {
				listen_port = atoi(argv[++i]);
				if (listen_port <= 0 || listen_port > 65535) {
					fprintf(stderr, "Errore: porta non valida %d (range 1-65535)\n", listen_port);
					return 1;
				}
				continue;
			}
			fprintf(stderr, "Errore: manca il valore per -p\n");
			return 1;
		}
	}

#if defined WIN32
	SetConsoleOutputCP(CP_UTF8);

	WSADATA wsa_data;
	int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
	if (result != NO_ERROR) {
		print_error("Errore: WSAStartup() fallita.\n");
		return 1;
	}
#endif

	// CREAZIONE SOCKET UDP
	// DIFFERENZA CHIAVE: SOCK_DGRAM invece di SOCK_STREAM
	int my_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (my_socket < 0) {
		print_error("Errore: creazione socket UDP fallita.\n");
		clearwinsock();
		return 1;
	}

	// CONFIGURAZIONE INDIRIZZO
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short)listen_port);
	server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

	// BIND
	if (bind(my_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		print_error("Errore: bind() fallita.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// Inizializza generatore casuale (IDENTICO AL TCP)
	initialize_random_generator();

	printf("Server UDP in ascolto sulla porta %d...\n", listen_port);

	// LOOP PRINCIPALE
	// DIFFERENZA CHIAVE: NO listen() e NO accept()
	while (1) {
		uint8_t recv_buffer[REQUEST_SIZE];
		struct sockaddr_in client_addr;

#if defined WIN32
		int client_addr_len = sizeof(client_addr);
#else
		socklen_t client_addr_len = sizeof(client_addr);
#endif

		// RICEZIONE DATAGRAM
		// DIFFERENZA CHIAVE: recvfrom() invece di recv()
		// Acquisisce automaticamente indirizzo client
		int bytes_received = recvfrom(my_socket, (char *)recv_buffer, REQUEST_SIZE, 0,
		                              (struct sockaddr *)&client_addr, &client_addr_len);

		if (bytes_received < 0) {
			print_error("Errore: recvfrom() fallita.\n");
			continue; // Continua ad ascoltare
		}

		if (bytes_received != REQUEST_SIZE) {
			fprintf(stderr, "Errore: ricevuti %d byte, attesi %d byte. \n", bytes_received, (int)REQUEST_SIZE);
			continue;
		}

		// RISOLUZIONE DNS CLIENT
		char client_hostname[256];
		char client_ip[16];

		resolve_client_address(&client_addr.sin_addr, client_hostname,
		                      sizeof(client_hostname), client_ip, sizeof(client_ip));

		// DESERIALIZZAZIONE
		weather_request_t request;
		if (deserialize_request(recv_buffer, &request) != 0) {
			print_error("Errore: deserializzazione fallita.\n");
			continue;
		}

		printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
		       client_hostname, client_ip, request.type, request.city);

		// VALIDAZIONE E GENERAZIONE RISPOSTA
		weather_response_t response;
		memset(&response, 0, sizeof(response));

		int validation_status = validate_request_server(&request);

		switch (validation_status) {
			case STATUS_SUCCESS:
				response.status = STATUS_SUCCESS;
				response.type = request.type;

				// Genera valore meteo appropriato
				switch (request.type) {
					case TYPE_TEMPERATURE:
						response.value = get_temperature();
						break;
					case TYPE_HUMIDITY:
						response.value = get_humidity();
						break;
					case TYPE_WIND:
						response.value = get_wind();
						break;
					case TYPE_PRESSURE:
						response.value = get_pressure();
						break;
					default:
						response.value = 0.0f;
						break;
				}
				break;

			case STATUS_CITY_NOT_FOUND:
				response.status = STATUS_CITY_NOT_FOUND;
				response.type = request.type;
				response.value = 0.0f;
				break;

			case STATUS_INVALID_REQUEST:
				response.status = STATUS_INVALID_REQUEST;
				response.type = request.type;
				response.value = 0.0f;
				break;

			default:
				response.status = STATUS_INVALID_REQUEST;
				response.type = '\0';
				response.value = 0.0f;
				break;
		}

		// SERIALIZZAZIONE
		uint8_t send_buffer[RESPONSE_SIZE];
		int serialized_len = serialize_response(&response, send_buffer);

		if (serialized_len < 0) {
			print_error("Errore: serializzazione fallita.\n");
			continue;
		}

		// INVIO RISPOSTA
		// DIFFERENZA CHIAVE: sendto() invece di send()
		// Usa indirizzo client acquisito da recvfrom()
		int bytes_sent = sendto(my_socket, (char *)send_buffer, serialized_len, 0,
		                        (struct sockaddr *)&client_addr, client_addr_len);

		if (bytes_sent != serialized_len) {
			print_error("Errore: sendto() fallita.\n");
			continue;
		}

		// Loop continua indefinitamente (il server non termina autonomamente)
	}

	// Codice mai raggiunto (server non termina autonomamente)
	printf("Server terminated.\n");
	closesocket(my_socket);
	clearwinsock();

	return 0;
}
