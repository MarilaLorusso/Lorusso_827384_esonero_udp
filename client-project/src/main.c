/*
 * main.c
 *
 * UDP Client - Weather Service
 */

#if defined WIN32
#include <winsock.h>
#include <windows.h>
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
#include <string.h>
#include <ctype.h>
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
 * Parsing della richiesta utente: "type city"
 */
int parse_weather_request(const char *input, weather_request_t *request) {
	// CONTROLLI DI SICUREZZA (IDENTICO TCP)
	if (input == NULL || request == NULL || strlen(input) < 3) {
		return 0; // Errore
	}

	// VALIDAZIONE TAB
	if (strchr(input, '\t') != NULL) {
		fprintf(stderr, "Errore: la richiesta non può contenere caratteri di tabulazione.\n");
		return 0;
	}

	// PARSING DEL TIPO
	request->type = input[0];

	// VALIDAZIONE PRIMO TOKEN SINGOLO CARATTERE
	// "Se il primo token contiene più di un carattere, il client deve segnalare errore"
	const char *first_space = strchr(input, ' ');
	if (!first_space) {
		fprintf(stderr, "Errore: formato richiesta invalido. Usa: \"type città\"\n");
		return 0;
	}

	size_t first_token_len = first_space - input;
	if (first_token_len != 1) {
		fprintf(stderr, "Errore: il tipo deve essere un singolo carattere ('t', 'h', 'w', 'p').\n");
		return 0;
	}

	// Parto dal carattere dopo il tipo
	const char *cursor = input + 1;

	// Salto TUTTI gli spazi
	while (*cursor == ' ') {
		cursor++;
	}

	// CONTROLLO CITTÀ VUOTA
	if (*cursor == '\0') {
		fprintf(stderr, "Errore: nome città mancante.\n");
		return 0;
	}

	// VALIDAZIONE LUNGHEZZA CITTÀ
	// "se il nome della città supera 63 caratteri, il client deve segnalare errore"
	size_t city_len = strlen(cursor);
	if (city_len >= 64) {
		fprintf(stderr, "Errore: nome città troppo lungo (massimo 63 caratteri).\n");
		return 0;
	}

	// Copio la città nel buffer della struct
	strncpy(request->city, cursor, sizeof(request->city) - 1);
	request->city[sizeof(request->city) - 1] = '\0';

	return 1; // Successo
}

/*
 * Serializzazione manuale della richiesta
 */
int serialize_request(const weather_request_t *request, uint8_t *buffer) {
	if (!request || !buffer) {
		return -1;
	}

	int offset = 0;

	// Campo type: 1 byte
	buffer[offset] = (uint8_t)request->type;
	offset += 1;

	// Campo city: 64 byte
	memcpy(buffer + offset, request->city, 64);
	offset += 64;

	return offset; // Ritorna 65 byte
}

/*
 * Deserializzazione della risposta
 */
int deserialize_response(const uint8_t *buffer, weather_response_t *response) {
	if (!buffer || !response) {
		return -1;
	}

	int offset = 0;

	// Campo status: 4 byte uint32_t - CONVERSIONE da network byte order
	uint32_t net_status;
	memcpy(&net_status, buffer + offset, sizeof(uint32_t));
	response->status = ntohl(net_status);
	offset += sizeof(uint32_t);

	// Campo type: 1 byte - nessuna conversione
	response->type = (char)buffer[offset];
	offset += 1;

	// Campo value: 4 byte float - CONVERSIONE da network byte order
	// Tecnica: float -> uint32_t -> ntohl() -> float
	uint32_t net_bits;
	memcpy(&net_bits, buffer + offset, sizeof(uint32_t));
	uint32_t host_bits = ntohl(net_bits);
	memcpy(&response->value, &host_bits, sizeof(float));
	offset += sizeof(float);

	return 0; // Successo
}

/*
 * Risoluzione DNS (hostname/IP -> nome e indirizzo)
 */
int resolve_host(const char *input, char *hostname_out, size_t hostname_size, char *ip_out, size_t ip_size) {
	if (!input || !hostname_out || !ip_out) {
		return -1;
	}

	struct hostent *host = NULL;
	struct in_addr addr;

	// Prova a convertire come IP
	addr.s_addr = inet_addr(input);

	if (addr.s_addr == INADDR_NONE) {
		// È un hostname -> risolvi con gethostbyname()
		host = gethostbyname(input);
		if (!host) {
			fprintf(stderr, "Errore: impossibile risolvere l'hostname '%s'.\n", input);
			return -1;
		}

		// Estrae il primo IP dalla lista
		struct in_addr *resolved_addr = (struct in_addr *)host->h_addr_list[0];
		strncpy(ip_out, inet_ntoa(*resolved_addr), ip_size - 1);
		ip_out[ip_size - 1] = '\0';

		// Nome canonico
		strncpy(hostname_out, host->h_name, hostname_size - 1);
		hostname_out[hostname_size - 1] = '\0';

	} else {
		// È un IP -> reverse lookup con gethostbyaddr()
		host = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);

		if (!host) {
			// Reverse lookup fallito: usa l'IP come hostname
			strncpy(hostname_out, input, hostname_size - 1);
			hostname_out[hostname_size - 1] = '\0';
		} else {
			strncpy(hostname_out, host->h_name, hostname_size - 1);
			hostname_out[hostname_size - 1] = '\0';
		}

		strncpy(ip_out, input, ip_size - 1);
		ip_out[ip_size - 1] = '\0';
	}

	return 0;
}

/*
 * Stampa il risultato formattato
 */
void print_result(const weather_response_t *response, const weather_request_t *request,
                  const char *server_name, const char *server_ip) {
	// Capitalizza prima lettera città
	char city_display[64];
	strncpy(city_display, request->city, 64);
	if (city_display[0] >= 'a' && city_display[0] <= 'z') {
		city_display[0] = city_display[0] - 'a' + 'A';
	}

	switch (response->status) {
		case STATUS_SUCCESS:
			printf("Ricevuto risultato dal server %s (ip %s). %s: ",
			       server_name, server_ip, city_display);

			switch (response->type) {
				case TYPE_TEMPERATURE:
					printf("Temperatura = %.1f°C\n", response->value);
					break;
				case TYPE_HUMIDITY:
					printf("Umidità = %.1f%%\n", response->value);
					break;
				case TYPE_WIND:
					printf("Vento = %.1f km/h\n", response->value);
					break;
				case TYPE_PRESSURE:
					printf("Pressione = %.1f hPa\n", response->value);
					break;
			}
			break;

		case STATUS_CITY_NOT_FOUND:
			printf("Ricevuto risultato dal server %s (ip %s). Città non disponibile\n",
			       server_name, server_ip);
			break;

		case STATUS_INVALID_REQUEST:
			printf("Ricevuto risultato dal server %s (ip %s). Richiesta non valida\n",
			       server_name, server_ip);
			break;
	}
}

int main(int argc, char *argv[]) {

	const char *server_address = "localhost";
	int server_port = SERVER_PORT;
	const char *request_string = NULL;

	// PARSING ARGOMENTI
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0) {
			if (i + 1 < argc) {
				server_address = argv[++i];
				continue;
			}
			fprintf(stderr, "Errore: manca il valore per -s\n");
			return 1;
		}

		if (strcmp(argv[i], "-p") == 0) {
			if (i + 1 < argc) {
				server_port = atoi(argv[++i]);
				if (server_port <= 0 || server_port > 65535) {
					fprintf(stderr, "Errore: porta non valida %d (range 1-65535)\n", server_port);
					return 1;
				}
				continue;
			}
			fprintf(stderr, "Errore: manca il valore per -p\n");
			return 1;
		}

		if (strcmp(argv[i], "-r") == 0) {
			if (i + 1 < argc) {
				request_string = argv[++i];
				continue;
			}
			fprintf(stderr, "Errore: manca il valore per -r\n");
			return 1;
		}

		if (argv[i][0] != '-' && request_string == NULL) {
			request_string = argv[i];
			continue;
		}
	}

	if (!request_string) {
		fprintf(stderr, "Errore: richiesta mancante.\n");
		fprintf(stderr, "Uso: %s [-s server] [-p port] -r \"type city\"\n", argv[0]);
		return 1;
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

	// PARSING RICHIESTA
	weather_request_t request;
	memset(&request, 0, sizeof(request));

	if (parse_weather_request(request_string, &request) == 0) {
		clearwinsock();
		return 1;
	}

	// RISOLUZIONE DNS
	char server_hostname[256];
	char server_ip[16];

	if (resolve_host(server_address, server_hostname, sizeof(server_hostname), server_ip, sizeof(server_ip)) != 0) {
		clearwinsock();
		return 1;
	}

	// CREAZIONE SOCKET UDP
	// DIFFERENZA: SOCK_DGRAM invece di SOCK_STREAM
	int my_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (my_socket < 0) {
		print_error("Errore: creazione socket UDP fallita.\n");
		clearwinsock();
		return 1;
	}

	// CONFIGURAZIONE INDIRIZZO SERVER
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short)server_port);
	server_addr.sin_addr.s_addr = inet_addr(server_ip);

	// SERIALIZZAZIONE
	uint8_t send_buffer[REQUEST_SIZE];
	int serialized_len = serialize_request(&request, send_buffer);
	if (serialized_len < 0) {
		print_error("Errore: serializzazione fallita.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// INVIO DATAGRAM
	// DIFFERENZA CHIAVE: sendto() invece di send()
	// NO connect() in UDP (connectionless)
	int bytes_sent = sendto(my_socket, (char *)send_buffer, serialized_len, 0,
	                        (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (bytes_sent != serialized_len) {
		print_error("Errore: sendto() fallita.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// RICEZIONE RISPOSTA
	// DIFFERENZA CHIAVE: recvfrom() invece di recv()
	uint8_t recv_buffer[RESPONSE_SIZE];
	struct sockaddr_in from_addr;

#if defined WIN32
	int from_len = sizeof(from_addr);
#else
	socklen_t from_len = sizeof(from_addr);
#endif

	int bytes_received = recvfrom(my_socket, (char *)recv_buffer, RESPONSE_SIZE, 0,
	                              (struct sockaddr *)&from_addr, &from_len);
	if (bytes_received <= 0) {
		print_error("Errore: recvfrom() fallita.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// VALIDAZIONE SORGENTE: verifica che la risposta venga dal server corretto
	if (from_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr) {
		fprintf(stderr, "Errore: ricevuto pacchetto da sorgente sconosciuta.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// DESERIALIZZAZIONE
	weather_response_t response;
	if (deserialize_response(recv_buffer, &response) != 0) {
		print_error("Errore: deserializzazione fallita.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// OUTPUT
	print_result(&response, &request, server_hostname, server_ip);

	// CHIUSURA
	closesocket(my_socket);
	printf("Client terminated.\n");
	clearwinsock();

	return 0;
}
