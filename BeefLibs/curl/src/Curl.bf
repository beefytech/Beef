using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
using System.Collections;

namespace CURL
{
	class Easy
	{
		const int32 cOptionLong = 0;
		const int32 cOptionString = 10000;
		const int32 cOptionObject = 10000;
		const int32 cOptionFunction = 20000;
		const int32 cOptionOffT = 30000;

		[AllowDuplicates]
		public enum Option
		{
			/* This is the FILE * or void * the regular output should be written to. */
			WriteData = cOptionObject + 1,  /* The full URL to get/put */
			URL = cOptionString + 2,  /* Port number to connect to, if other than default. */
			Port = cOptionLong + 3,
			/* Name of proxy to use. */
			Proxy = cOptionString + 4,
			/* "user:password;options" to use when fetching. */
			UserPwd = cOptionString + 5,
			/* "user:password" to use with proxy. */
			ProxyUserPwd = cOptionString + 6,
			/* Range to get, specified as an ASCII string. */
			Range = cOptionString + 7,
			/* not used */
			/* Specified file stream to upload from (use as input): */
			ReadData = cOptionObject + 9,
			/* Buffer to receive error messages in, must be at least CURL_ERROR_SIZE
			 * bytes big. If this is not used, error messages go to stderr instead: */
			ErrorBuffer = cOptionObject + 10,
			/* Function that will be called to store the output (instead of fwrite). The
			 * parameters will use fwrite() syntax, make sure to follow them. */
			WriteFunction = cOptionFunction + 11,
			/* Function that will be called to read the input (instead of fread). The
			 * parameters will use fread() syntax, make sure to follow them. */
			ReadFunction = cOptionFunction + 12,
			/* Time-out the read operation after this amount of seconds */
			Timeout = cOptionLong + 13,
			/* If the CURLOPT_INFILE is used, this can be used to inform libcurl about
			 * how large the file being sent really is. That allows better error
			 * checking and better verifies that the upload was successful. -1 means
			 * unknown size.
			 *
			 * For large file support, there is also a _LARGE version of the key
			 * which takes an off_t type, allowing platforms with larger off_t
			 * sizes to handle larger files.  See below for INFILESIZE_LARGE.
			 */
			InfileSize = cOptionLong + 14,
			/* POST static input fields. */
			Postfields = cOptionObject + 15,
			/* Set the referrer page (needed by some CGIs) */
			Referer = cOptionString + 16,
			/* Set the FTP PORT string (interface name, named or numerical IP address)
			   Use i.e '-' to use default address. */
			FTPPort = cOptionString + 17,
			/* Set the User-Agent string (examined by some CGIs) */
			UserAgent = cOptionString + 18,
			/* If the download receives less than "low speed limit" bytes/second
			 * during "low speed time" seconds, the operations is aborted.
			 * You could i.e if you have a pretty high speed connection, abort if
			 * it is less than 2000 bytes/sec during 20 seconds.
			 */
			/* Set the "low speed limit" */
			LowSpeedLimit = cOptionLong + 19,
			/* Set the "low speed time" */
			LowSpeedTime = cOptionLong + 20,
			/* Set the continuation offset.
			 *
			 * Note there is also a _LARGE version of this key which uses
			 * off_t types, allowing for large file offsets on platforms which
			 * use larger-than-32-bit off_t's.  Look below for RESUME_FROM_LARGE.
			 */
			ResumeFrom = cOptionLong + 21,
			/* Set cookie in request: */
			Cookie = cOptionString + 22,
			/* This points to a linked list of headers, struct curl_slist kind. This
			   list is also used for RTSP (in spite of its name) */
			HTTPHeader = cOptionObject + 23,
			/* This points to a linked list of post entries, struct curl_HTTPpost */
			HTTPpost = cOptionObject + 24,
			/* name of the file keeping your private SSL-certificate */
			SSLCert = cOptionString + 25,
			/* password for the SSL or SSH private key */
			KeyPasswd = cOptionString + 26,
			/* send TYPE parameter? */
			CRLF = cOptionLong + 27,
			/* send linked-list of QUOTE commands */
			Quote = cOptionObject + 28,
			/* send FILE * or void * to store headers to, if you use a callback it
			   is simply passed to the callback unmodified */
			HeaderData = cOptionObject + 29,
			/* point to a file to read the initial cookies from, also enables
			   "cookie awareness" */
			CookieFile = cOptionString + 31,
			/* What version to specifically try to use.
			   See CURL_SSLVERSION defines below. */
			SSLVersion = cOptionLong + 32,
			/* What kind of HTTP time condition to use, see defines */
			TimeCondition = cOptionLong + 33,
			/* Time to use with the above condition. Specified in number of seconds
			   since 1 Jan 1970 */
			TimeValue = cOptionLong + 34,
			/* 35 = OBSOLETE */
			/* Custom request, for customizing the get command like
			   HTTP: DELETE, TRACE and others
			   FTP: to use a different list command
			   */
			Customrequest = cOptionString + 36,
			/* FILE handle to use instead of stderr */
			Stderr = cOptionObject + 37,
			/* 38 is not used */
			/* send linked-list of post-transfer QUOTE commands */
			Postquote = cOptionObject + 39,
			Verbose = cOptionLong + 41,      /* talk a lot */
			Header = cOptionLong + 42,       /* throw the header out too */
			NoProgress = cOptionLong + 43,   /* shut off the progress meter */
			NoBody = cOptionLong + 44,       /* use HEAD to get HTTP document */
			FailOnError = cOptionLong + 45,  /* no output on HTTP error codes >= 400 */
			Upload = cOptionLong + 46,       /* this is an upload */
			Post = cOptionLong + 47,         /* HTTP POST method */
			Dirlistonly = cOptionLong + 48,  /* bare names when listing directories */
			Append = cOptionLong + 50,       /* Append instead of overwrite on upload! */
			/* Specify whether to read the user+password from the .netrc or the URL.
			 * This must be one of the CURL_NETRC_* enums below. */
			NetRC = cOptionLong + 51,
			FollowLocation = cOptionLong + 52,  /* use Location: Luke! */
			TransferText = cOptionLong + 53, /* transfer data in text/ASCII format */
			Put = cOptionLong + 54,          /* HTTP PUT */
			/* 55 = OBSOLETE */
			/* DEPRECATED
			 * Function that will be called instead of the internal progress display
			 * function. This function should be defined as the curl_progress_callback
			 * prototype defines. */
			ProgressFunction = cOptionFunction + 56,
			/* Data passed to the CURLOPT_PROGRESSFUNCTION and CURLOPT_XFERINFOFUNCTION
			   callbacks */
			ProgressData = cOptionObject + 57,
			XferInfoData = cOptionObject + 57,
			/* We want the referrer field set automatically when following locations */
			AutoReferer = cOptionLong + 58,
			/* Port of the proxy, can be set in the proxy string as well with:
			   "[host]:[port]" */
			ProxyPort = cOptionLong + 59,
			/* size of the POST input data, if strlen() is not good to use */
			PostfieldSize = cOptionLong + 60,
			/* tunnel non-HTTP operations through a HTTP proxy */
			HTTPProxyTunnel = cOptionLong + 61,
			/* Set the interface string to use as outgoing network interface */
			Interface = cOptionString + 62,
			/* Set the krb4/5 security level, this also enables krb4/5 awareness.  This
			 * is a string, 'clear', 'safe', 'confidential' or 'private'.  If the string
			 * is set but doesn't match one of these, 'private' will be used.  */
			KrbLevel = cOptionString + 63,
			/* Set if we should verify the peer in SSL handshake, set 1 to verify. */
			SSLVerifyPeer = cOptionLong + 64,
			/* The CApath or CAfile used to validate the peer certificate
			   this option is used only if SSL_VERIFYPEER is true */
			CAInfo = cOptionString + 65,
			/* 66 = OBSOLETE */  /* 67 = OBSOLETE */
			/* Maximum number of HTTP redirects to follow */
			MaxRedirs = cOptionLong + 68,
			/* Pass a long set to 1 to get the date of the requested document (if
			   possible)! Pass a zero to shut it off. */
			FileTime = cOptionLong + 69,
			/* This points to a linked list of telnet options */
			TelnetOptions = cOptionObject + 70,
			/* Max amount of cached alive connections */
			MaxConnects = cOptionLong + 71,
			/* Set to explicitly use a new connection for the upcoming transfer.
			   Do not use this unless you're absolutely sure of this, as it makes the
			   operation slower and is less friendly for the network. */
			FreshConnect = cOptionLong + 74,
			/* Set to explicitly forbid the upcoming transfer's connection to be re-used
			   when done. Do not use this unless you're absolutely sure of this, as it
			   makes the operation slower and is less friendly for the network. */
			ForbidReuse = cOptionLong + 75,
			/* Set to a file name that contains random data for libcurl to use to
			   seed the random engine when doing SSL connects. */
			RandomFile = cOptionString + 76,
			/* Set to the Entropy Gathering Daemon socket pathname */
			EGDSocket = cOptionString + 77,
			/* Time-out connect operations after this amount of seconds, if connects are
			   OK within this time, then fine... This only aborts the connect phase. */
			ConnectTimeout = cOptionLong + 78,
			/* Function that will be called to store headers (instead of fwrite). The
			 * parameters will use fwrite() syntax, make sure to follow them. */
			HeaderFunction = cOptionFunction + 79,
			/* Set this to force the HTTP request to get back to GET. Only really usable
			   if POST, PUT or a custom request have been used first.
			 */
			HTTPGet = cOptionLong + 80,
			/* Set if we should verify the Common name from the peer certificate in SSL
			 * handshake, set 1 to check existence, 2 to ensure that it matches the
			 * provided hostname. */
			SSLVerifyHost = cOptionLong + 81,
			/* Specify which file name to write all known cookies in after completed
			   operation. Set file name to "-" (dash) to make it go to stdout. */
			CookieJar = cOptionString + 82,
			/* Specify which SSL ciphers to use */
			SSLCipherList = cOptionString + 83,
			/* Specify which HTTP version to use! This must be set to one of the
			   CURL_HTTP_VERSION* enums set below. */
			HTTPVersion = cOptionLong + 84,
			/* Specifically switch on or off the FTP engine's use of the EPSV command. By
			   default, that one will always be attempted before the more traditional
			   PASV command. */
			FTPUseEPSV = cOptionLong + 85,
			/* type of the file keeping your SSL-certificate ("DER", "PEM", "ENG") */
			SSLCertType = cOptionString + 86,
			/* name of the file keeping your private SSL-key */
			SSLKey = cOptionString + 87,
			/* type of the file keeping your private SSL-key ("DER", "PEM", "ENG") */
			SSLKeyType = cOptionString + 88,
			/* crypto engine for the SSL-sub system */
			SSLEngine = cOptionString + 89,
			/* set the crypto engine for the SSL-sub system as default
			   the param has no meaning...
			 */
			SSLEngineDefault = cOptionLong + 90,
			/* Non-zero value means to use the global dns cache */
			DnsUseGlobalCache = cOptionLong + 91, /* DEPRECATED, do not use! */
			/* DNS cache timeout */
			DnsCacheTimeout = cOptionLong + 92,
			/* send linked-list of pre-transfer QUOTE commands */
			PreQuote = cOptionObject + 93,
			/* set the debug function */
			DebugFunction = cOptionFunction + 94,
			/* set the data for the debug function */
			DebugData = cOptionObject + 95,
			/* mark this as start of a cookie session */
			CookieSession = cOptionLong + 96,
			/* The CApath directory used to validate the peer certificate
			   this option is used only if SSL_VERIFYPEER is true */
			CAPath = cOptionString + 97,
			/* Instruct libcurl to use a smaller receive buffer */
			BufferSize = cOptionLong + 98,
			/* Instruct libcurl to not use any signal/alarm handlers, even when using
			   timeouts. This option is useful for multi-threaded applications.
			   See libcurl-the-guide for more background information. */
			NoSignal = cOptionLong + 99,
			/* Provide a CURLShare for mutexing non-ts data */
			Share = cOptionObject + 100,
			/* indicates type of proxy. accepted values are CURLPROXY_HTTP (default,
			   CURLPROXY_HTTPS, CURLPROXY_SOCKS4, CURLPROXY_SOCKS4A and
			   CURLPROXY_SOCKS5. */
			ProxyType = cOptionLong + 101,
			/* Set the Accept-Encoding string. Use this to tell a server you would like
			   the response to be compressed. Before 7.21.6, this was known as
			   CURLOPT_ENCODING */
			AcceptEncoding = cOptionString + 102,
			/* Set pointer to private data */
			Private = cOptionObject + 103,
			/* Set aliases for HTTP 200 in the HTTP Response header */
			HTTP200Aliases = cOptionObject + 104,
			/* Continue to send authentication (user+password) when following locations,
			   even when hostname changed. This can potentially send off the name
			   and password to whatever host the server decides. */
			UnrestrictedAuth = cOptionLong + 105,
			/* Specifically switch on or off the FTP engine's use of the EPRT command (
			   it also disables the LPRT attempt). By default, those ones will always be
			   attempted before the good old traditional PORT command. */
			FTPUseEPRT = cOptionLong + 106,
			/* Set this to a bitmask value to enable the particular authentications
			   methods you like. Use this in combination with CURLOPT_USERPWD.
			   Note that setting multiple bits may cause extra network round-trips. */
			HTTPAuth = cOptionLong + 107,
			/* Set the SSL context callback function, currently only for OpenSSL SSL_ctx
			   in second argument. The function must be matching the
			   curl_SSL_ctx_callback proto. */
			SSLCTXFunction = cOptionFunction + 108,
			/* Set the userdata for the SSL context callback function's third
			   argument */
			SSLCTXData = cOptionObject + 109,
			/* FTP Option that causes missing dirs to be created on the remote server.
			   In 7.19.4 we introduced the convenience enums for this option using the
			   CURLFTP_CREATE_DIR prefix.
			*/
			FTPCreateMissingDirs = cOptionLong + 110,
			/* Set this to a bitmask value to enable the particular authentications
			   methods you like. Use this in combination with CURLOPT_PROXYUSERPWD.
			   Note that setting multiple bits may cause extra network round-trips. */
			ProxyAuth = cOptionLong + 111,
			/* FTP option that changes the timeout, in seconds, associated with
			   getting a response.  This is different from transfer timeout time and
			   essentially places a demand on the FTP server to acknowledge commands
			   in a timely manner. */
			FTPResponseTimeout = cOptionLong + 112,
			/* Set this option to one of the CURL_IPRESOLVE_* defines (see below) to
			   tell libcurl to resolve names to those IP versions only. This only has
			   affect on systems with support for more than one, i.e IPv4 _and_ IPv6. */
			IPResolve = cOptionLong + 113,
			/* Set this option to limit the size of a file that will be downloaded from
			   an HTTP or FTP server.

			   Note there is also _LARGE version which adds large file support for
			   platforms which have larger off_t sizes.  See MAXFILESIZE_LARGE below. */
			MaxFileSize = cOptionLong + 114,
			/* See the comment for INFILESIZE above, but in short, specifies
			 * the size of the file being uploaded.  -1 means unknown.
			 */
			InFileSizeLarge = cOptionOffT + 115,
			/* Sets the continuation offset.  There is also a LONG version of this;
			 * look above for RESUME_FROM.
			 */
			ResumeFromLarge = cOptionOffT + 116,
			/* Sets the maximum size of data that will be downloaded from
			 * an HTTP or FTP server.  See MAXFILESIZE above for the LONG version.
			 */
			MaxFileSizeLarge = cOptionOffT + 117,
			/* Set this option to the file name of your .netrc file you want libcurl
			   to parse (using the CURLOPT_NETRC option). If not set, libcurl will do
			   a poor attempt to find the user's home directory and check for a .netrc
			   file in there. */
			NetRCFile = cOptionString + 118,
			/* Enable SSL/TLS for FTP, pick one of:
			   CURLUSESSL_TRY     - try using SSL, proceed anyway otherwise
			   CURLUSESSL_CONTROL - SSL for the control connection or fail
			   CURLUSESSL_ALL     - SSL for all communication or fail
			*/
			UseSSL = cOptionLong + 119,
			/* The _LARGE version of the standard POSTFIELDSIZE option */
			PostfieldsizeLarge = cOptionOffT + 120,
			/* Enable/disable the TCP Nagle algorithm */
			TCPNoDelay = cOptionLong + 121,
			/* 122 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */  /* 123 OBSOLETE. Gone in 7.16.0 */  /* 124 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */  /* 125 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */  /* 126 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */  /* 127 OBSOLETE. Gone in 7.16.0 */  /* 128 OBSOLETE. Gone in 7.16.0 */
			/* When FTP over SSL/TLS is selected (with CURLOPT_USE_SSL, this option
			   can be used to change libcurl's default action which is to first try
			   "AUTH SSL" and then "AUTH TLS" in this order, and proceed when a OK
			   response has been received.

			   Available parameters are:
			   CURLFTPAUTH_DEFAULT - let libcurl decide
			   CURLFTPAUTH_SSL     - try "AUTH SSL" first, then TLS
			   CURLFTPAUTH_TLS     - try "AUTH TLS" first, then SSL
			*/
			FTPSSLauth = cOptionLong + 129,
			IoctlFunction = cOptionFunction + 130,
			IoctlData = cOptionObject + 131,
			/* 132 OBSOLETE. Gone in 7.16.0 */  /* 133 OBSOLETE. Gone in 7.16.0 */
			/* zero terminated string for pass on to the FTP server when asked for
			   "account" info */
			FTPAccount = cOptionString + 134,
			/* feed cookie into cookie engine */
			CookieList = cOptionString + 135,
			/* ignore Content-Length */
			IgnoreContentLength = cOptionLong + 136,
			/* Set to non-zero to skip the IP address received in a 227 PASV FTP server
			   response. Typically used for FTP-SSL purposes but is not restricted to
			   that. libcurl will then instead use the same IP address it used for the
			   control connection. */
			FTPSkipPasvIP = cOptionLong + 137,
			/* Select "file method" to use when doing FTP, see the curl_FTPmethod
			   above. */
			FTPFileMethod = cOptionLong + 138,
			/* Local port number to bind the socket to */
			LocalPort = cOptionLong + 139,
			/* Number of ports to try, including the first one set with LOCALPORT.
			   Thus, setting it to 1 will make no additional attempts but the first.
			*/
			LocalPortRange = cOptionLong + 140,
			/* no transfer, set up connection and let application use the socket by
			   extracting it with CURLINFO_LASTSOCKET */
			ConnectOnly = cOptionLong + 141,
			/* Function that will be called to convert from the
			   network encoding (instead of using the iconv calls in libcurl) */
			ConvFromNetworkFunction = cOptionFunction + 142,
			/* Function that will be called to convert to the
			   network encoding (instead of using the iconv calls in libcurl) */
			ConvToNetworkFunction = cOptionFunction + 143,
			/* Function that will be called to convert from UTF8
			   (instead of using the iconv calls in libcurl)
			   Note that this is used only for SSL certificate processing */
			ConvFromUTF8Function = cOptionFunction + 144,
			/* if the connection proceeds too quickly then need to slow it down */  /* limit-rate: maximum number of bytes per second to send or receive */
			MaxSendSpeedLarge = cOptionOffT + 145,
			MaxRecvSpeedLarge = cOptionOffT + 146,
			/* Pointer to command string to send if USER/PASS fails. */
			FTPAlternativeToUser = cOptionString + 147,
			/* callback function for setting socket options */
			SockoptFunction = cOptionFunction + 148,
			SockoptData = cOptionObject + 149,
			/* set to 0 to disable session ID re-use for this transfer, default is
			   enabled (== 1) */
			SSLSessionIDCache = cOptionLong + 150,
			/* allowed SSH authentication methods */
			SSHAuthTypes = cOptionLong + 151,
			/* Used by scp/sftp to do public/private key authentication */
			SSHPublicKeyfile = cOptionString + 152,
			SSHPrivateKeyfile = cOptionString + 153,
			/* Send CCC (Clear Command Channel) after authentication */
			FtpSSLCcc = cOptionLong + 154,
			/* Same as TIMEOUT and CONNECTTIMEOUT, but with ms resolution */
			TimeoutMs = cOptionLong + 155,
			ConnecttimeoutMs = cOptionLong + 156,
			/* set to zero to disable the libcurl's decoding and thus pass the raw body
			   data to the application even when it is encoded/compressed */
			HTTPTransferDecoding = cOptionLong + 157,
			HTTPContentDecoding = cOptionLong + 158,
			/* Permission used when creating new files and directories on the remote
			   server for protocols that support it, SFTP/SCP/FILE */
			NewFilePerms = cOptionLong + 159,
			NewDirectoryPerms = cOptionLong + 160,
			/* Set the behaviour of POST when redirecting. Values must be set to one
			   of CURL_REDIR* defines below. This used to be called CURLOPT_POST301 */
			POSTREDIR = cOptionLong + 161,
			/* used by scp/sftp to verify the host's public key */
			SSHHostPublicKeyMd5 = cOptionString + 162,

			/* Callback function for opening socket (instead of socket(2)). Optionally,
			   callback is able change the address or refuse to connect returning
			   CURL_SOCKET_BAD.  The callback should have type
			   curl_opensocket_callback */
			Opensocketfunction = cOptionFunction + 163,
			Opensocketdata = cOptionObject + 164,
			/* POST volatile input fields. */
			Copypostfields = cOptionObject + 165,
			/* set transfer mode (;type=<a|i>) when doing FTP via an HTTP proxy */
			ProxyTransferMode = cOptionLong + 166,
			/* Callback function for seeking in the input stream */
			Seekfunction = cOptionFunction + 167,
			Seekdata = cOptionObject + 168,
			/* CRL file */
			Crlfile = cOptionString + 169,
			/* Issuer certificate */
			Issuercert = cOptionString + 170,
			/* (IPv6) Address scope */
			AddressScope = cOptionLong + 171,
			/* Collect certificate chain info and allow it to get retrievable with
			   CURLINFO_CERTINFO after the transfer is complete. */
			Certinfo = cOptionLong + 172,
			/* "name" and "pwd" to use when fetching. */
			Username = cOptionString + 173,
			Password = cOptionString + 174,
			  /* "name" and "pwd" to use with Proxy when fetching. */
			Proxyusername = cOptionString + 175,
			Proxypassword = cOptionString + 176,
			/* Comma separated list of hostnames defining no-proxy zones. These should
			   match both hostnames directly, and hostnames within a domain. For
			   example, local.com will match local.com and www.local.com, but NOT
			   notlocal.com or www.notlocal.com. For compatibility with other
			   implementations of this, .local.com will be considered to be the same as
			   local.com. A single * is the only valid wildcard, and effectively
			   disables the use of proxy. */
			Noproxy = cOptionString + 177,
			/* block size for TFTP transfers */
			TFTPBlksize = cOptionLong + 178,
			/* Socks Service */
			Socks5GssapiService = cOptionString + 179, /* DEPRECATED, do not use! */
			/* Socks Service */
			Socks5GssapiNec = cOptionLong + 180,
			/* set the bitmask for the protocols that are allowed to be used for the
			   transfer, which thus helps the app which takes URLs from users or other
			   external inputs and want to restrict what protocol(s) to deal
			   with. Defaults to CURLPROTO_ALL. */
			Protocols = cOptionLong + 181,
			/* set the bitmask for the protocols that libcurl is allowed to follow to,
			   as a subset of the CURLOPT_PROTOCOLS ones. That means the protocol needs
			   to be set in both bitmasks to be allowed to get redirected to. Defaults
			   to all protocols except FILE and SCP. */
			RedirProtocols = cOptionLong + 182,
			/* set the SSH knownhost file name to use */
			SSHKnownhosts = cOptionString + 183,
			/* set the SSH host key callback, must point to a curl_SSHkeycallback
			   function */
			SSHKeyfunction = cOptionFunction + 184,
			/* set the SSH host key callback custom pointer */
			SSHKeydata = cOptionObject + 185,
			/* set the SMTP mail originator */
			MailFrom = cOptionString + 186,
			/* set the list of SMTP mail receiver(s) */
			MailRcpt = cOptionObject + 187,
			/* FTP: send PRET before PASV */
			FtpUsePret = cOptionLong + 188,
			/* RTSP request method (OPTIONS, SETUP, PLAY, etc...) */
			RTSPRequest = cOptionLong + 189,
			/* The RTSP session identifier */
			RTSPSessionID = cOptionString + 190,
			/* The RTSP stream URI */
			RTSPStreamUri = cOptionString + 191,
			/* The Transport: header to use in RTSP requests */
			RTSPTransport = cOptionString + 192,
			/* Manually initialize the client RTSP CSeq for this handle */
			RTSPClientCseq = cOptionLong + 193,
			/* Manually initialize the server RTSP CSeq for this handle */
			RTSPServerCseq = cOptionLong + 194,
			/* The stream to pass to INTERLEAVEFUNCTION. */
			Interleavedata = cOptionObject + 195,
			/* Let the application define a custom write method for RTP data */
			Interleavefunction = cOptionFunction + 196,
			/* Turn on wildcard matching */
			Wildcardmatch = cOptionLong + 197,
			/* Directory matching callback called before downloading of an
			   individual file (chunk) started */
			ChunkBgnFunction = cOptionFunction + 198,
			/* Directory matching callback called after the file (chunk)
			   was downloaded, or skipped */
			ChunkEndFunction = cOptionFunction + 199,
			/* Change match (fnmatch-like) callback for wildcard matching */
			FnmatchFunction = cOptionFunction + 200,
			/* Let the application define custom chunk data pointer */
			ChunkData = cOptionObject + 201,
			/* FNMATCH_FUNCTION user pointer */
			FnmatchData = cOptionObject + 202,
			/* send linked-list of name:port:address sets */
			Resolve = cOptionObject + 203,
			/* Set a username for authenticated TLS */
			TLSAuthUsername = cOptionString + 204,
			/* Set a password for authenticated TLS */
			TLSAuthPassword = cOptionString + 205,
			/* Set authentication type for authenticated TLS */
			TLSAuthType = cOptionString + 206,

			/* Set to 1 to enable the "TE:" header in HTTP requests to ask for
			   compressed transfer-encoded responses. Set to 0 to disable the use of TE:
			   in outgoing requests. The current default is 0, but it might change in a
			   future libcurl release.

			   libcurl will ask for the compressed methods it knows of, and if that
			   isn't any, it will not ask for transfer-encoding at all even if this
			   option is set to 1.

			*/
			TransferEncoding = cOptionLong + 207,
			/* Callback function for closing socket (instead of close(2)). The callback
			   should have type curl_closesocket_callback */
			Closesocketfunction = cOptionFunction + 208,
			Closesocketdata = cOptionObject + 209,
			/* allow GSSAPI credential delegation */
			GssapiDelegation = cOptionLong + 210,
			/* Set the name servers to use for DNS resolution */
			DnsServers = cOptionString + 211,
			/* Time-out accept operations (currently for FTP only) after this amount
			   of milliseconds. */
			AccepttimeoutMs = cOptionLong + 212,
			/* Set TCP keepalive */
			TCPKeepalive = cOptionLong + 213,
			/* non-universal keepalive knobs (Linux, AIX, HP-UX, more) */
			TCPKeepidle = cOptionLong + 214,
			TCPKeepintvl = cOptionLong + 215,
			/* Enable/disable specific SSL features with a bitmask, see CURLSSLOPT_* */
			SSLOptions = cOptionLong + 216,
			/* Set the SMTP auth originator */
			MailAuth = cOptionString + 217,
			/* Enable/disable SASL initial response */
			SASlIr = cOptionLong + 218,
			/* Function that will be called instead of the internal progress display
			 * function. This function should be defined as the curl_xferinfo_callback
			 * prototype defines. (Deprecates CURLOPT_PROGRESSFUNCTION) */
			XferInfoFunction = cOptionFunction + 219,
			/* The XOAUTH2 bearer token */
			Xoauth2Bearer = cOptionString + 220,
			/* Set the interface string to use as outgoing network
			 * interface for DNS requests.
			 * Only supported by the c-ares DNS backend */
			DnsInterface = cOptionString + 221,
			/* Set the local IPv4 address to use for outgoing DNS requests.
			 * Only supported by the c-ares DNS backend */
			DnsLocalIp4 = cOptionString + 222,
			/* Set the local IPv4 address to use for outgoing DNS requests.
			 * Only supported by the c-ares DNS backend */
			DnsLocalIp6 = cOptionString + 223,
			/* Set authentication options directly */
			LoginOptions = cOptionString + 224,
			/* Enable/disable TLS NPN extension (http2 over SSL might fail without) */
			SSLEnableNpn = cOptionLong + 225,
			/* Enable/disable TLS ALPN extension (http2 over SSL might fail without) */
			SSLEnableAlpn = cOptionLong + 226,
			/* Time to wait for a response to a HTTP request containing an
			 * Expect: 100-continue header before sending the data anyway. */
			Expect100TimeoutMs = cOptionLong + 227,
			/* This points to a linked list of headers used for proxy requests only,
			   struct curl_slist kind */
			ProxyHeader = cOptionObject + 228,
			/* Pass in a bitmask of "header options" */
			HeaderOpt = cOptionLong + 229,
			/* The public key in DER form used to validate the peer public key
			   this option is used only if SSL_VERIFYPEER is true */
			Pinnedpublickey = cOptionString + 230,
			/* Path to Unix domain socket */
			UnixSocketPath = cOptionString + 231,
			/* Set if we should verify the certificate status. */
			SSLVerifystatus = cOptionLong + 232,
			/* Set if we should enable TLS false start. */
			SSLFalsestart = cOptionLong + 233,
			/* Do not squash dot-dot sequences */
			PathAsIs = cOptionLong + 234,
			/* Proxy Service Name */
			ProxyServiceName = cOptionString + 235,
			/* Service Name */
			ServiceName = cOptionString + 236,
			/* Wait/don't wait for pipe/mutex to clarify */
			Pipewait = cOptionLong + 237,
			/* Set the protocol used when curl is given a URL without a protocol */
			DefaultProtocol = cOptionString + 238,
			/* Set stream weight, 1 - 256 (default is 16) */
			StreamWeight = cOptionLong + 239,
			/* Set stream dependency on another CURL handle */
			StreamDepends = cOptionObject + 240,
			/* Set E-xclusive stream dependency on another CURL handle */
			StreamDependsE = cOptionObject + 241,
			/* Do not send any TFTP option requests to the server */
			TFTPNoOptions = cOptionLong + 242,
			/* Linked-list of host:port:connect-to-host:connect-to-port,
			   overrides the URL's host:port (only for the network layer) */
			ConnectTo = cOptionObject + 243,
			/* Set TCP Fast Open */
			TCPFastOpen = cOptionLong + 244,
			/* Continue to send data if the server responds early with an
			 * HTTP status code >= 300 */
			KeepSendingOnError = cOptionLong + 245,
			/* The CApath or CAfile used to validate the proxy certificate
			   this option is used only if PROXY_SSL_VERIFYPEER is true */
			ProxyCainfo = cOptionString + 246,
			/* The CApath directory used to validate the proxy certificate
			   this option is used only if PROXY_SSL_VERIFYPEER is true */
			ProxyCapath = cOptionString + 247,
			/* Set if we should verify the proxy in SSL handshake,
			   set 1 to verify. */
			ProxySSLVerifypeer = cOptionLong + 248,
			/* Set if we should verify the Common name from the proxy certificate in SSL
			 * handshake, set 1 to check existence, 2 to ensure that it matches
			 * the provided hostname. */
			ProxySSLVerifyhost = cOptionLong + 249,
			/* What version to specifically try to use for proxy.
			   See CURL_SSLVERSION defines below. */
			ProxySSLversion = cOptionLong + 250,
			/* Set a username for authenticated TLS for proxy */
			ProxyTLSAuthUsername = cOptionString + 251,
			/* Set a password for authenticated TLS for proxy */
			ProxyTLSAuthPassword = cOptionString + 252,
			/* Set authentication type for authenticated TLS for proxy */
			ProxyTLSAuthType = cOptionString + 253,
			/* name of the file keeping your private SSL-certificate for proxy */
			ProxySSLCert = cOptionString + 254,
			/* type of the file keeping your SSL-certificate ("DER", "PEM", "ENG") for
			   proxy */
			ProxySSLCerttype = cOptionString + 255,
			/* name of the file keeping your private SSL-key for proxy */
			ProxySSLkey = cOptionString + 256,
			/* type of the file keeping your private SSL-key ("DER", "PEM", "ENG") for
			   proxy */
			ProxySSLkeytype = cOptionString + 257,
			/* password for the SSL private key for proxy */
			ProxyKeypasswd = cOptionString + 258,
			/* Specify which SSL ciphers to use for proxy */
			ProxySSLCipherList = cOptionString + 259,
			/* CRL file for proxy */
			ProxyCrlfile = cOptionString + 260,
			/* Enable/disable specific SSL features with a bitmask for proxy, see
			   CURLSSLOPT_* */
			ProxySSLOptions = cOptionLong + 261,
			/* Name of pre proxy to use. */
			PreProxy = cOptionString + 262,
			/* The public key in DER form used to validate the proxy public key
			   this option is used only if PROXY_SSL_VERIFYPEER is true */
			ProxyPinnedpublickey = cOptionString + 263,
			/* Path to an abstract Unix domain socket */
			AbstractUnixSocket = cOptionString + 264,
			/* Suppress proxy CONNECT response headers from user callbacks */
			SuppressConnectHeaders = cOptionLong + 265,
		}

		public enum IPResolve
		{
			V4 = 1,
			V6 = 2
		}

		const int32 cInfoString   = 0x100000;
		const int32 cInfoLong     = 0x200000;
		const int32 cInfoDouble   = 0x300000;
		const int32 cInfoSList    = 0x400000;
		const int32 cInfoSocket   = 0x500000;
		const int32 cInfoMask     = 0x0fffff;
		const int32 cInfoTypeMask = 0xf00000;

		public enum CurlInfo
		{
		    EffectiveUrl     = cInfoString + 1,
			ResponseCode     = cInfoLong   + 2,
			TotalTime        = cInfoDouble + 3,
			NameLookupTime   = cInfoDouble + 4,
			ConnectTime      = cInfoDouble + 5,
			PreTransferTime  = cInfoDouble + 6,
			SizeUpload       = cInfoDouble + 7,
			SiadDownload     = cInfoDouble + 8,
			SpeedDownload = cInfoDouble   + 9,
			SpeedUpload      = cInfoDouble + 10,
			HeaderSize       = cInfoLong   + 11,
			RequestSize = cInfoLong        + 12,
			SSLVerifyResult  = cInfoLong   + 13,
			FileTime         = cInfoLong   + 14,
			ContentLengthDownload = cInfoDouble + 15,
			ContentLengthUpload = cInfoDouble + 16,
			StartTransferTime= cInfoDouble + 17,
			ContentType      = cInfoString + 18,
			RedirectTime     = cInfoDouble + 19,
			RedirectCount    = cInfoLong   + 20,
			Private          = cInfoString + 21,
			HTTPConntectCode = cInfoLong   + 22,
			HTTPAuthAvail    = cInfoLong   + 23,
			ProxyAuthAvail   = cInfoLong   + 24,
			OsErrno          = cInfoLong   + 25,
			NumConnects      = cInfoLong   + 26,
			SSLEngines       = cInfoSList  + 27,
			CookieList       = cInfoSList  + 28,
			LastSocket       = cInfoLong   + 29,
			FTPEntryPath     = cInfoString + 30,
			RedirectURL      = cInfoString + 31,
			PrimaryIP        = cInfoString + 32,
			AppConnectTime   = cInfoDouble + 33,
			CertInfo         = cInfoSList  + 34,
			ConditionUnmet   = cInfoLong   + 35,
			RTSPSessionID    = cInfoString + 36,
			RTSPClientSEQ    = cInfoLong   + 37,
			RTSPServerCSEQ   = cInfoLong   + 38,
			RTSPCSEQRecv     = cInfoLong   + 39,
			PriamryPort      = cInfoLong   + 40,
			LocalIP          = cInfoString + 41,
			LocalPort        = cInfoLong   + 42,
			TLSSession       = cInfoSList  + 43,
			ActiveSocket     = cInfoSocket + 44,
			TLSSSLPtr        = cInfoSList  + 45,
			HTTPVersion      = cInfoLong   + 46,
			ProxySSLVerifyResult = cInfoLong + 47,
			Protocol         = cInfoLong   + 48,
			Scheme           = cInfoString + 49,
		};

		public enum ReturnCode
		{
			Ok = 0,
			UnsupportedProtocol,    /* 1 */
			FailedInit,             /* 2 */
			URLMalformat,           /* 3 */
			NotBuiltIn,            /* 4 - [was obsoleted in August 2007 for
			                                  7.17.0, reused in April 2011 for 7.21.5] */
			CouldntResolveProxy,   /* 5 */
			CouldntResolveHost,    /* 6 */
			CouldntConnect,         /* 7 */
			WeirdServerReply,      /* 8 */
			RemoteAccessDenied,    /* 9 a service was denied by the server
			                                  due to lack of access - when login fails
			                                  this is not returned. */
			FTPAcceptFailed,       /* 10 - [was obsoleted in April 2006 for
			                                  7.15.4, reused in Dec 2011 for 7.24.0]*/
			FTPWeirdPassReply,    /* 11 */
			FTPAcceptTimeout,      /* 12 - timeout occurred accepting server
			                                  [was obsoleted in August 2007 for 7.17.0,
			                                  reused in Dec 2011 for 7.24.0]*/
			FTPWeirdPasvReply,    /* 13 */
			FTPWeird227Format,    /* 14 */
			FTPCantGetHost,       /* 15 */
			HTTP2,                   /* 16 - A problem in the HTTP2 framing layer.
			                                  [was obsoleted in August 2007 for 7.17.0,
			                                  reused in July 2014 for 7.38.0] */
			FTPCouldntSetType,    /* 17 */
			PartialFile,            /* 18 */
			FTPCouldntRetrFile,   /* 19 */
			Obsolete20,              /* 20 - NOT USED */
			QuoteError,             /* 21 - quote command failure */
			HTTPReturnedError,     /* 22 */
			WriteError,             /* 23 */
			Obsolete24,              /* 24 - NOT USED */
			UploadFailed,           /* 25 - failed upload "command" */
			ReadError,              /* 26 - couldn't open/read from file */
			OutOfMemory,           /* 27 */
			/* Note: OUT_OF_MEMORY may sometimes indicate a conversion error
			         instead of a memory allocation error if CURL_DOES_CONVERSIONS
			         is defined
			*/
			OperationTimedout,      /* 28 - the timeout time was reached */
			Obsolete29,              /* 29 - NOT USED */
			FTPPortFailed,         /* 30 - FTP PORT operation failed */
			FTPCouldntUseRest,    /* 31 - the REST command failed */
			Obsolete32,              /* 32 - NOT USED */
			RangeError,             /* 33 - RANGE "command" didn't work */
			HTTPPostError,         /* 34 */
			SSLConnectError,       /* 35 - wrong when connecting with SSL */
			BadDownloadResume,     /* 36 - couldn't resume download */
			FileCouldntReadFile,  /* 37 */
			LdapCannotBind,        /* 38 */
			LdapSearchFailed,      /* 39 */
			Obsolete40,              /* 40 - NOT USED */
			FunctionNotFound,      /* 41 - NOT USED starting with 7.53.0 */
			AbortedByCallback,     /* 42 */
			BadFunctionArgument,   /* 43 */
			Obsolete44,              /* 44 - NOT USED */
			InterfaceFailed,        /* 45 - CURLOPT_INTERFACE failed */
			Obsolete46,              /* 46 - NOT USED */
			TooManyRedirects,      /* 47 - catch endless re-direct loops */
			UnknownOption,          /* 48 - User specified an unknown option */
			TelnetOptionSyntax,    /* 49 - Malformed telnet option */
			Obsolete50,              /* 50 - NOT USED */
			PeerFailedVerification, /* 51 - peer's certificate or fingerprint wasn't verified fine */
			GotNothing,             /* 52 - when this is a specific error */
			SSLEngineNotfound,     /* 53 - SSL crypto engine not found */
			SSLEngineSetfailed,    /* 54 - can not set SSL crypto engine as
			                                  default */
			SendError,              /* 55 - failed sending network data */
			RecvError,              /* 56 - failure in receiving network data */
			Obsolete57,              /* 57 - NOT IN USE */
			SSLCertproblem,         /* 58 - problem with the local certificate */
			SSLCipher,              /* 59 - couldn't use specified cipher */
			SSLCacert,              /* 60 - problem with the CA cert (path?) */
			BadContentEncoding,    /* 61 - Unrecognized/bad encoding */
			LdapInvalidURL,        /* 62 - Invalid LDAP URL */
			FilesizeExceeded,       /* 63 - Maximum file size exceeded */
			UseSSLFailed,          /* 64 - Requested FTP SSL level failed */
			SendFailRewind,        /* 65 - Sending the data requires a rewind
			                                  that failed */
			SSLEngineInitfailed,   /* 66 - failed to initialise ENGINE */
			LoginDenied,            /* 67 - user, password or similar was not
			                                  accepted and we failed to login */
			TFTPNotfound,           /* 68 - file not found on server */
			TFTPPerm,               /* 69 - permission problem on server */
			RemoteDiskFull,        /* 70 - out of disk space on server */
			TFTPIllegal,            /* 71 - Illegal TFTP operation */
			TFTPUnknownid,          /* 72 - Unknown transfer ID */
			RemoteFileExists,      /* 73 - File already exists */
			TFTPNosuchuser,         /* 74 - No such user */
			ConvFailed,             /* 75 - conversion failed */
			ConvReqd,               /* 76 - caller must register conversion
			                                  callbacks using curl_easy_setopt options
			                                  CURLOPT_CONV_FROM_NETWORK_FUNCTION,
			                                  CURLOPT_CONV_TO_NETWORK_FUNCTION, and
			                                  CURLOPT_CONV_FROM_UTF8_FUNCTION */
			SSLCacertBadfile,      /* 77 - could not load CACERT file, missing
			                                  or wrong format */
			RemoteFileNotFound,   /* 78 - remote file not found */
			SSH,                     /* 79 - error from the SSH layer, somewhat
			                                  generic so the error message will be of
			                                  interest when this has happened */

			SSLShutdownFailed,     /* 80 - Failed to shut down the SSL
			                                  connection */
			Again,                   /* 81 - socket is not ready for send/recv,
			                                  wait till it's ready and try again (Added
			                                  in 7.18.2) */
			SSLCrlBadfile,         /* 82 - could not load CRL file, missing or
			                                  wrong format (Added in 7.19.0) */
			SSLIssuerError,        /* 83 - Issuer check failed.  (Added in
			                                  7.19.0) */
			FTPPretFailed,         /* 84 - a PRET command failed */
			RtspCseqError,         /* 85 - mismatch of RTSP CSeq numbers */
			RtspSessionError,      /* 86 - mismatch of RTSP Session Ids */
			FTPBadFileList,       /* 87 - unable to parse FTP file list */
			ChunkFailed,            /* 88 - chunk callback reported error */
			NoConnectionAvailable, /* 89 - No connection available, the session will be queued */
			SSLPinnedpubkeynotmatch, /* 90 - specified pinned public key did not
			                                   match */
			SSLInvalidcertstatus,   /* 91 - invalid certificate status */
			HTTP2Stream,            /* 92 - stream error in HTTP/2 framing layer */
		}

		public struct SList;

		[CLink, CallingConvention(.Stdcall)]
		static extern void* curl_easy_init();

		[CLink, CallingConvention(.Stdcall)]
		static extern void curl_free(void* ptr);

		[CLink, CallingConvention(.Stdcall)]
		static extern char8* curl_easy_escape(void* curl, char8* str, int32 length);

		[CLink, CallingConvention(.Stdcall)]
		static extern char8* curl_easy_unescape(void* curl, char8* input, int32 inlength, int32* outlength);

		[CLink, CallingConvention(.Stdcall)]
		static extern int curl_easy_setopt(void* curl, int option, int optVal);

		[CLink, CallingConvention(.Stdcall)]
		static extern void* curl_easy_duphandle(void* curl);

		[CLink, CallingConvention(.Stdcall)]
		static extern void* curl_easy_cleanup(void* curl);

		[CLink, CallingConvention(.Stdcall)]
		static extern int curl_easy_perform(void* curl);

		[CLink, CallingConvention(.Stdcall)]
		static extern void* curl_easy_getinfo(void* curl, CurlInfo info, void* ptr);

		[CLink, CallingConvention(.Stdcall)]
		static extern void* curl_easy_reset(void* curl);

		[CLink, CallingConvention(.Stdcall)]
		static extern SList* curl_slist_append(SList* list, char8* val);

		[CLink, CallingConvention(.Stdcall)]
		static extern void curl_slist_free_all(SList* list);

		void* mCURL;

		public this()
		{
			mCURL = curl_easy_init();
		}

		public this(Easy initFrom)
		{
			mCURL = curl_easy_duphandle(initFrom.mCURL);
		}

		public ~this()
		{
			curl_easy_cleanup(mCURL);
		}

		Result<void, ReturnCode> WrapResult(ReturnCode returnCode)
		{
			if (returnCode == .Ok)
				return .Ok;
			return .Err(returnCode);
		}

		public Result<void, ReturnCode> SetOpt(Option option, bool val)
		{
			Debug.Assert((int)option / 10000 == 0);
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, val ? 1 : 0));
		}

		public Result<void, ReturnCode> SetOpt(Option option, String val)
		{
			Debug.Assert((int)option / 10000 == 1);
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, (int)(void*)val.CStr()));
		}

		public Result<void, ReturnCode> SetOpt(Option option, StringView val)
		{
			Debug.Assert((int)option / 10000 == 1);
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, (int)(void*)scope String(4096)..Append(val).CStr()));
		}

		public Result<void, ReturnCode> SetOpt(Option option, int val)
		{
			Debug.Assert(((int)option / 10000 == 0) || ((int)option / 10000 == 3));
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, val));
		}

		public Result<void, ReturnCode> SetOpt(Option option, void* val)
		{
			Debug.Assert((int)option / 10000 == 1);
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, (int)val));
		}

		public Result<void, ReturnCode> SetOptFunc(Option option, void* funcPtr)
		{
			Debug.Assert((int)option / 10000 == 2);
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, (int)funcPtr));
		}

		public Result<void, ReturnCode> SetOpt(Option option, SList* list)
		{
			Debug.Assert(((int)option / 10000 == 1));
			return WrapResult((ReturnCode)curl_easy_setopt(mCURL, (int)option, (int)(void*)list));
		}

		public Result<void> GetInfo(CurlInfo info, String val)
		{
			char8* ptr = null;
			curl_easy_getinfo(mCURL, info, &ptr);
			if (ptr == null)
				return .Err;
			val.Append(ptr);
			return .Ok;
		}

		public Result<void, ReturnCode> Perform()
		{
			return WrapResult((ReturnCode)curl_easy_perform(mCURL));
		}

		public void Reset()
		{
			curl_easy_reset(mCURL);
		}

		public SList* Add(SList* list, StringView val)
		{
			return curl_slist_append(list, val.ToScopeCStr!());
		}

		public void Free(SList* list)
		{
			curl_slist_free_all(list);
		}

		public void Escape(StringView str, String outStr)
		{
			char8* ptr = curl_easy_escape(mCURL, str.Ptr, (.)str.Length);
			if (ptr != null)
			{
				outStr.Append(ptr);
				curl_free(ptr);
			}
		}

		public void Unescape(StringView str, String outStr)
		{
			int32 outLen = 0;
			char8* ptr = curl_easy_unescape(mCURL, str.Ptr, (.)str.Length, &outLen);
			if (ptr != null)
			{
				outStr.Append(ptr, outLen);
				curl_free(ptr);
			}
		}
	}
}