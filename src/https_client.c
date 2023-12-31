#include "https_client.h"
#include "url_parser.h"
#include <stdarg.h>

char *get_ip_as_string(struct sockaddr *address)
{
  char *ip_string = malloc(INET6_ADDRSTRLEN);
  if (address->sa_family == AF_INET)
  {
    inet_ntop(AF_INET6, (struct in6_addr *)address, ip_string, INET6_ADDRSTRLEN);
    return ip_string;
  }

  inet_ntop(AF_INET, (struct in_addr *)address, ip_string, INET_ADDRSTRLEN);
  return ip_string;
}

int http_client_create_socket(char *address_, char *port, struct sockaddr **host)
{
  int status, sock;
  struct addrinfo hints;
  struct addrinfo *res, *p;

  memset(&hints, 0, sizeof hints);

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_CANONNAME | AF_INET;

  status = getaddrinfo(address_, port, &hints, &res);

  if (status != 0)
  {
    fprintf(stderr, "%s\n", gai_strerror(status));
    return -1;
  }

  for (p = res; p; p = p->ai_next)
  {
    get_ip_as_string(p->ai_addr);

    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

    if (sock < 0)
      continue;

    if (!connect(sock, p->ai_addr, p->ai_addrlen))
    {
      *host = malloc(sizeof p->ai_addr);
      memcpy(*host, p->ai_addr, p->ai_addrlen);
      break;
    }

    perror("connect");
  }

  if (p == NULL)
  {
    return -1;
  }

  freeaddrinfo(res);
  return sock;
}

SSL *http_client_create_ssl(char *address_, SSL_CTX *ctx, int sock)
{
  SSL *ssl = NULL;
  (void)SSL_library_init();
  SSL_load_error_strings();
  // OPENSSL_config(NULL);

#if defined(OPENSSL_THREADS)
// to do
#endif

  int res;

  ssl = SSL_new(ctx);
  if (ssl == NULL)
  {
    return NULL;
  }

  const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
  SSL_CTX_set_options(ctx, flags);

  res = SSL_set_tlsext_host_name(ssl, address_);
  if (res != 1)
  {
    return NULL;
  }

  const char *preffered_ciphers = "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4";

  res = SSL_set_cipher_list(ssl, preffered_ciphers);

  if (res != 1)
  {
    return NULL;
  }

  res = SSL_set_fd(ssl, sock);
  if (res != 1)
  {
    return NULL;
  }

  if (ssl == NULL)
  {
    exit(1);
  }

  printf("Attempting to connect to %s\n", address_);
  res = SSL_connect(ssl);
  if (res != 1)
  {
    return NULL;
  }

  printf("Performing a TLS handshake with %s\n", address_);
  res = SSL_do_handshake(ssl);
  if (res != 1)
  {
    return NULL;
  }

  return ssl;
}

http_client *http_client_create()
{

  http_client *client = malloc(sizeof(http_client));

  client->headers = NULL;

  client->url = NULL;

  client->http_version = malloc(10 * sizeof(char));

  client->port = NULL;

  client->address = NULL;

  client->body = NULL;
  client->response_headers = NULL;

  strcpy(client->http_version, "HTTP/1.1");

  client->file_size = 0;

  return client;
}

bool http_client_set_url(char *_url, http_client *client)
{
  if (!_url || !client)
    return false;

  url *c_url = url_parse(_url);

  if (c_url == NULL)
    return false;

  client->url = c_url->route;

  client->address = c_url->address;
  http_client_set_header("Host", c_url->address, client);

  if (c_url->port)
  {
    client->port = c_url->port;
  }
  else
  {
    http_client_set_port("443", client);
  }

  free(c_url);

  return true;
}

bool http_client_set_method(methods m, http_client *client)
{
  switch (m)
  {
  case GET:
    return http_client_set_method_str("GET", client);
    break;
  case POST:
    return http_client_set_method_str("POST", client);
    break;
  case PATCH:
    return http_client_set_method_str("PATCH", client);
    break;
  case PUT:
    return http_client_set_method_str("PUT", client);
    break;
  case DELETE:
    return http_client_set_method_str("DELETE", client);
    break;
  case OPTIONS:
    return http_client_set_method_str("OPTIONS", client);
    break;
  default:
    return http_client_set_method_str("GET", client);
  }

  return false;
}

bool http_client_set_method_str(char *m, http_client *client)
{
  if (!client || !m)
    return false;
  int l = strlen(m);
  client->method = malloc(l + 1);
  strcpy(client->method, m);
  return true;
}

bool http_client_set_address(char *address, http_client *client)
{
  if (!client || !address)
    return false;
  int l = strlen(address);
  client->address = malloc(l + 1);
  strcpy(client->address, address);
  return true;
}

bool http_client_set_port(char *port, http_client *client)
{
  if (!client || !port)
    return false;
  int l = strlen(port);
  client->port = malloc(l + 1);
  strcpy(client->port, port);
  return true;
}

bool http_client_set_header(char *key, char *value, http_client *client)
{
  if (!key || !value || !client)
    return false;

  if (client->headers == NULL)
    client->headers = map_create();

  map_put(client->headers, key, value);
  return true;
}

bool http_client_append_file(char *path, http_client *client)
{
  if (!client || client->body)
    return false;

  FILE *fptr;
  size_t file_size;

  if (!(fptr = fopen(path, "rb")))
    return false;

  fseek(fptr, 0, SEEK_END);

  file_size = ftell(fptr);
  client->file_size = file_size;

  client->body = malloc(file_size + 1);
  bzero(client->body, file_size + 1);

  rewind(fptr);
  size_t read_bytes = 0;
  bool out = true;
  size_t offset = 0;

  while ((read_bytes = fread(client->body + offset, 1, 100, fptr)))
  {
    if (read_bytes != 100)
    {
      if (ferror(fptr))
      {
        free(client->body);
        out = false;
        break;
      }
    }
    offset += 100;
  }

  return out;
}

bool http_client_append_string(char *str, http_client *client)
{
  if (!str || !client || client->body)
    return false;

  int len = strlen(str);

  client->body = malloc(len + 1);

  if (!client->body)
    return false;

  client->file_size = len;

  strcpy(client->body, str);

  return true;
}

bool http_client_connect(http_client *client)
{
  if (client == NULL)
    return false;

  if (!client->url || !client->method)
  {
    puts("=========url err===========");
    return false;
  }

  // if(!client->body)
  //{
  //   http_client_set_header("Content-length","0",client);
  // }

  SSL_CTX *ctx = NULL;
  (void)SSL_library_init();
  SSL_load_error_strings();

  const SSL_METHOD *method = SSLv23_method();

  if (method == NULL)
  {
    puts("=======method err==========");
    return false;
  }

  ctx = SSL_CTX_new(method);

  if (ctx == NULL)
  {
    puts("=========== ctx err ===========");
    return false;
  }

  int sock;
  struct sockaddr *remote_host = NULL;

  if ((sock = http_client_create_socket(client->address, client->port, &remote_host)) == -1)
  {
    puts("==============socket err===========");
    return false;
  }

  SSL *ssl = NULL;
  char *header = http_client_write_header(client);

  if ((ssl = http_client_create_ssl(client->address, ctx, sock)) == NULL)
  {
    puts("SSl failed");
    return false;
  }

  if ((SSL_write(ssl, header, strlen(header))) == -1)
  {
    puts("===========write err=============");
    return false;
  }

  bool out = true;

  if (client->body)
  {
    int size;
    int offset = 0;
    int rem = 100;
    size_t total_sent = 0;

    while (true)
    {
      int b_sent;

      if (client->file_size <= 100)
      {
        size = client->file_size;
      }
      else
      {
        size = rem;
      }

      b_sent = SSL_write(ssl, client->body + offset, size);

      total_sent += b_sent;

      if (b_sent < 1)
      {
        if (b_sent == -1)
        {
          out = false;
        }
        puts("++++ write err+++++");
        break;
      }

      if (total_sent >= client->file_size)
        break;

      if (client->file_size <= 100)
        break;

      rem = client->file_size - total_sent;

      if (rem < 100)
      {
        size = rem;
      }
      else
      {
        size = 100;
      }

      offset += 100;
    }
  }

  out = http_client_receive_response(ssl, client);
  client->context = ctx;
  client->handle = ssl;
  client->stream = stream_init(client->handle);

  return out;
}

bool http_client_receive_response(SSL *sock, http_client *client)
{
  char recv_buf[1] = {0}, end_of_header[] = "\r\n\r\n";
  int bytes_received, header_size = 0, marker = 0;
  string_t *b = string_create();

  map_t *http_res = NULL;

  bool out = true;

  while (true)
  {
    if (header_size == 8096)
    {
      puts("too large header");
      break;
    }

    bytes_received = SSL_read(sock, recv_buf, 1);

    if (bytes_received <= 0)
    {
      if (bytes_received < 0)
      {
        perror("wah ");
      }

      puts("Connection closed");
      out = false;
      break;
    }

    string_append(b, recv_buf[0]);

    if (recv_buf[0] == end_of_header[marker])
      marker++;
    else
      marker = 0;

    if (marker == 4)
    {

      if ((http_res = parse_http_response(b->chars)) == NULL)
      {
        out = false;
        break;
      }

      client->response_headers = http_res;

      break;
    }

    header_size++;
  }

  return out;
}

void dbg_client(http_client *ct)
{
  puts(ct->address);
  puts(ct->body);
  puts(ct->http_version);
  puts(ct->method);
  puts(ct->port);
}

void join_headers(char *key, char *value, string_t *str)
{
  string_concat(str, key, strlen(key));
  string_append(str, ':');
  string_append(str, ' ');
  string_concat(str, value, strlen(value));
  string_concat(str, "\r\n", 2);
}

char *http_client_write_header(http_client *ct)
{
  string_t *head = string_create();
  char onst[500];

  sprintf(onst, "%s %s %s\r\n", ct->method, ct->url, ct->http_version);

  string_concat(head, onst, strlen(onst));

  if (ct->headers == NULL)
  {
    string_concat(head, "\r\n", 2);
    char *chd = head->chars;
    free(head);
    return chd;
  }

  char *rawheaders = for_each_map(ct->headers, join_headers);

  string_concat(head, rawheaders, strlen(rawheaders));

  string_concat(head, "\r\n", 2);

  char *chd = head->chars;
  free(head);

  return chd;
}

bool http_client_set_host(struct sockaddr *host, http_client *client)
{
  char host_name[1024];
  char service[50];
  int status;

  if ((status = getnameinfo(host, sizeof(struct sockaddr_in), host_name, sizeof host_name, service, sizeof service, 0)) != 0)
  {
    return false;
  }

  free(client);

  // int port = http_client_get_service_port(service);

  // char host_port[1074];

  // sprintf(host_port,"%s:%d",host_name,port);
  return true;
}

int http_client_get_service_port(char *service_name)
{
  struct servent *sv = NULL;

  char proto[4] = "tcp";

  sv = getservbyname(service_name, proto);

  if (sv == NULL)
    return -1;
  // getservbyname(service_name,"TCP");
  return ntohs(sv->s_port);
}

void http_client_destroy(http_client *client)
{
  if (client == NULL)
    return;

  if (client->address)
    free(client->address);
  if (client->body)
    free(client->body);
  if (client->url)
    free(client->url);
  if (client->headers)
    map_destroy(&(client->headers));
  if (client->response_headers)
    map_destroy(&(client->response_headers));
  if (client->port)
    free(client->port);
  if (client->method)
    free(client->method);
  if (client->http_version)
    free(client->http_version);

  if (client->context)
  {
    SSL_free(client->handle);
    SSL_CTX_free(client->context);
  }

  free(client);
}

map_t *parse_http_response(char *req)
{
  int len = strlen(req);
  req = string_removechar('\r', req, len);
  strAL *lines = split('\n', req, strlen(req));

  string_array_list_delete(lines, lines->size - 1);
  string_array_list_delete(lines, lines->size - 1);

  strAL *vl = lines;

  char *firstLine = string_array_list_get(vl, 0);

  strAL *vc = split_lim(' ', firstLine, strlen(firstLine), 3);

  if (vc->size < 3)
  {
    fprintf(stderr, "Invalid response\n");
    return NULL;
  }

  map_t *map = map_create();

  map_put(
      map,
      "http-version",
      string_array_list_get(vc, 0));

  map_put(
      map,
      "status-code",
      string_array_list_get(vc, 1));

  map_put(
      map,
      "code-name",
      string_array_list_get(vc, 2));

  string_arraylist_destroy(&vc);

  for (size_t i = 1; i < vl->size; i++)
  {
    char *thisLine = string_array_list_get(vl, i);
    vc = split_lim(':', thisLine, strlen(thisLine), 2);
    if (vc->size != 2)
    {
      string_arraylist_destroy(&vc);
      continue;
    }

    char *ss = trim(string_array_list_get(vc, 1));
    char *sk = trim(string_array_list_get(vc, 0));

    char *sklow = string_to_lower(sk);

    char *sslow = string_to_lower(ss);

    if (!strcmp(sklow, "sec-websocket-key"))
    {
      map_put(map, sklow, ss);
    }
    else
    {

      map_put(map, sklow, sslow);
    }

    free(sslow);
    free(sklow);
    free(ss);
    free(sk);

    string_arraylist_destroy(&vc);
  }

  free(req);
  string_arraylist_destroy(&lines);
  return map;
}

ssize_t http_client_read(http_client *client, char *buff, size_t bytesToRead)
{
  return stream_read(client->stream, buff, bytesToRead);
}

ssize_t http_client_read_chunks(http_client *client, char **buff)
{

  string_t *line = stream_read_line(client->stream, "\r\n", false);

  strAL *split_line = split(';', line->chars, line->size);

  size_t chunkSize = strtoul(string_array_list_get(split_line, 0), NULL, 16);

  if (chunkSize == 0)
  {

    char waste[10] = {};

    stream_read(client->stream, waste, 2);

    *buff = NULL;
    client->stream->finished = true;
    return 0;
  }

  char *chunk = malloc(chunkSize + 5);
#include <strings.h>
  bzero(chunk, chunkSize + 5);

  ssize_t r = 0;

  if (chunk)
  {

    r = stream_read(client->stream, chunk, chunkSize);

    if (r < 1)
    {
      if (r == -10)
      {
        return -10;
      }
      return r;
    }

    *buff = chunk;
    char waste[10] = {};

    ssize_t waste_len = stream_read(client->stream, waste, 2);

    assert(waste_len == 2);
    assert(strncmp(waste, "\r\n", 2) == 0);
  }
  else
  {
    puts("malloc error");
    abort();
  }

  return r;
}

void http_client_restart(http_client *client)
{
  if (client->address)
    free(client->address);
  if (client->body)
    free(client->body);

  if (client->headers)
  {
  }

  if (client->url)
    free(client->url);
  if (client->response_headers)
    map_destroy(&(client->response_headers));
  if (client->port)

    if (client->context)
    {
      int fd = SSL_get_fd(client->handle);
      SSL_free(client->handle);
      close(fd);
      SSL_CTX_free(client->context);
    }

  client->stream = NULL;
}

bool http_client_send(http_client *client)
{
  if (http_client_connect(client))
  {
    char *status_code = map_get(client->response_headers, "status-code");

    if (status_code)
    {

      if ((strcmp(status_code, "302") == 0) || (strcmp(status_code, "301") == 0))
      {
        char *redirect_url = map_get(client->response_headers, "location");
        char *prev_method = string_create_copy(client->method);

        printf("Redirecting to: %s\n", redirect_url);

        if (redirect_url)
        {
          char *url_cpy = string_create_copy(redirect_url);
          char *rplced = replace_html_entity(url_cpy);
          char *prev_address = string_create_copy(client->address);

          http_client_restart(client);

          if (rplced[0] == '/')
          {
            client->url = rplced;
            client->method = prev_method;
            client->address = prev_address;
          }
          else
          {
            free(prev_address);
            map_delete(client->headers, "Host");
            free(client->port);
            http_client_set_url(rplced, client);
            client->method = prev_method;
          }

          return http_client_send(client);
        }
        else
        {
          return false;
        }
      }
      else
      {
        char *clen = map_get(client->response_headers, "content-length");
        char *trenc = map_get(client->response_headers, "transfer-encoding");

        client->stream = stream_init(client->handle);

        if (clen)
        {
          client->content_length = strtol(clen, NULL, 10);
          if (client->content_length == 0)
          {
            client->stream->finished = true;
          }
        }
        else if (trenc)
        {
          client->chunked_body = true;
        }
        else
        {
          puts("Length not specified");
          return false;
        }

        return true;
      }
    }
    else
    {
      return false;
    }

    return true;
  }
  else
  {
    return false;
  }
}

void http_client_read_to_file(http_client *client, char *filename)
{

  FILE *file = fopen(filename, "wb");

  if (file == NULL)
  {
    puts("Failed to open file");
    return;
  }

  if (!client->stream->finished)
  {
    if (client->chunked_body)
    {

      char *buff = NULL;
      ssize_t chunklen;
      while ((chunklen = http_client_read_chunks(client, &buff)) > 0)
      {
        fwrite(buff, chunklen, 1, file);
        free(buff);
      }
    }
    else
    {
      size_t rem = client->content_length;
      ssize_t r;
      ssize_t to_read_now = rem > 1024 ? 1024 : rem;
      char buff[1025] = {0};
      while ((r = stream_read(client->stream, buff, to_read_now)) > 0)
      {
        rem -= r;

        fwrite(buff, r, 1, file);

        if ((ssize_t)rem <= to_read_now)
        {
          to_read_now = rem;
        }

        if (rem <= 0)
          break;
      }
    }
  }

  fclose(file);
}