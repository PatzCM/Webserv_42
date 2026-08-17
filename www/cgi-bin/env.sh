#!/bin/sh
# Demo CGI script: prints a few CGI environment variables as plain text.

echo "Content-Type: text/plain; charset=utf-8"
echo ""
echo "CGI environment (shell CGI):"
echo "----------------------------"
echo "REQUEST_METHOD=$REQUEST_METHOD"
echo "QUERY_STRING=$QUERY_STRING"
echo "SCRIPT_FILENAME=$SCRIPT_FILENAME"
echo "SERVER_PROTOCOL=$SERVER_PROTOCOL"
echo "SERVER_NAME=$SERVER_NAME"
echo "SERVER_PORT=$SERVER_PORT"
echo "REMOTE_ADDR=$REMOTE_ADDR"
echo "GATEWAY_INTERFACE=$GATEWAY_INTERFACE"
echo "CONTENT_LENGTH=$CONTENT_LENGTH"
echo "HTTP_HOST=$HTTP_HOST"
echo "HTTP_USER_AGENT=$HTTP_USER_AGENT"
