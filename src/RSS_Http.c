#include "RSS_Http.h"

#include "RSS.h"
#include "RSS_Buffer.h"
#include "RSS_Http.h"

#ifndef RSS_NO_HTTP_SUPPORT

#if defined(_WIN32) && defined(_MSC_VER)
# pragma comment(lib, "Ws2_32.lib")
#else
# warning "Link socket library to the project"
#endif

RSS_Url* RSS_create_url(const RSS_char* url)
{
	size_t			len;
	const RSS_char*	tmp_host;
	const RSS_char*	tmp_path;
	RSS_Url*		rss_url;

	if(!url)
		return NULL;
	
	/* create RSS_Url to return */
	rss_url = (RSS_Url*)malloc(sizeof(RSS_Url));
	
	/* cut the http:// */
	tmp_host = RSS_strstr(url, RSS_text("http://"));
	if(tmp_host)
		tmp_host += 7; /* TODO: check if it will work also on UTF-8 */
	else
		tmp_host = url;

	/* find path */
	tmp_path = RSS_strstr(tmp_host, RSS_text("/"));
	rss_url->path = tmp_path ? RSS_str2char(tmp_path) : strdup("/");

	/* find host */
	len = RSS_strlen(tmp_host) - strlen(rss_url->path);
	if(!tmp_path)
		len += 2;
	if(len > 0)
	{
		rss_url->host = RSS_str2char(tmp_host);
		rss_url->host[len] = 0;
	}
	else
	{
		/* no host info, url is useless */
		if(rss_url->path)
			free(rss_url->path);
		free(rss_url);
		return NULL;
	}

	return rss_url;
}

void RSS_free_url(RSS_Url* url)
{
	if(!url)
		return;

	if(url->host)
		free(url->host);

	if(url->path);
		free(url->path);

	free(url);
}

RSS_Http_error RSS_http_get_page(const RSS_Url* url, char** buffer)
{
#ifdef _WIN32
	WSADATA		WSAData;
#endif
	SOCKET		sock;
	struct hostent*	remoteHost;
	SOCKADDR_IN	sin;
	char*		header_buff;
	int			s; /* for recv size */
	int			received;
	int			buffer_size;
	char		recv_buff[4096];
	char*		data;
	char*		tmp_buff;

	if(!url || !url->host || !url->path)
		return RSS_HTTP_BAD_ARG;

	/* Windows only winsock init */
#ifdef _WIN32
	if(WSAStartup(MAKEWORD(2,0), &WSAData) != 0)
		return RSS_HTTP_WSASTARTUP;
#endif

	/* Create socket */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == INVALID_SOCKET)
		return RSS_HTTP_SOCKET;

	/* Get hosts IP */
	remoteHost = gethostbyname(url->host);		
	if(remoteHost == NULL)
	{
		closesocket(sock);
		return RSS_HTTP_GETHOSTBYNAME;
	}

	if(remoteHost->h_addrtype != AF_INET)
	{
		closesocket(sock);
		return RSS_HTTP_NOT_IPV4;
	}

	/* Connect */
	memset(&sin, 0, sizeof(SOCKADDR_IN));
	sin.sin_addr.s_addr = *(u_long *) remoteHost->h_addr;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(80);

	if(connect(sock, (SOCKADDR *)&sin, sizeof(sin)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return RSS_HTTP_CONN_FAILED;
	}

	/* Send headers */
	s = sizeof(RSS_HTTP_HEADER1) + 
		sizeof(RSS_HTTP_HEADER2) +
		sizeof(RSS_HTTP_HEADER3) +
		strlen(url->host) +
		strlen(url->path) + 3;
	header_buff = (char*)malloc(s+1);
	sprintf(header_buff, 
		RSS_HTTP_HEADER1 \
		"%s" \
		RSS_HTTP_HEADER2 \
		"%s" \
		RSS_HTTP_HEADER3, url->path, url->host);
	
	if(send(sock, header_buff, strlen(header_buff), 0) <= 0)
	{
		free(header_buff);
		closesocket(sock);
		return RSS_HTTP_SEND_FAILED;
	}
	free(header_buff);

	/* Get response */
	received = 0;
	tmp_buff = (char*)malloc(RSS_HTTP_INITIAL_BUFFER_SIZE);
	buffer_size = RSS_HTTP_INITIAL_BUFFER_SIZE;
	while((s = recv(sock, recv_buff, 4096, 0)) != 0)
	{
		if(received + s >= buffer_size)
		{
			char*	new_buf;

			new_buf = (char*)malloc(buffer_size << 1);
			if(!new_buf)
			{
				free(tmp_buff);
				return RSS_HTTP_PAGE_TOO_BIG;
			}

			memcpy(new_buf, tmp_buff, buffer_size);
			buffer_size <<= 1;
			free(tmp_buff);
			tmp_buff = new_buf;
		}

		memcpy(tmp_buff+received, recv_buff, s);
		received += s;
	}
	tmp_buff[received] = 0;

	/* Check return code */
	if(received < 13)
	{
		closesocket(sock);
		free(tmp_buff);
		return RSS_HTTP_NOT_200;
	}

	if(tmp_buff[9] != '2' || tmp_buff[10] != '0' || tmp_buff[11] != '0')
	{
		closesocket(sock);
		free(tmp_buff);
		return RSS_HTTP_NOT_200;
	}
	
	data = strstr(tmp_buff, "\r\n\r\n");
	if(!data)
	{
		closesocket(sock);
		free(tmp_buff);
		return RSS_HTTP_NO_DATA;
	}

	*buffer = strdup(data + 4);

	closesocket(sock);
	free(tmp_buff);
#ifdef _WIN32
	WSACleanup();
#endif

	return RSS_HTTP_OK;
}

#endif