import http.server
import socketserver

# handler = http.server.SimpleHTTPRequestsHandler

class handler(http.server.BaseHTTPRequestHandler):
	def do_GET(self):
		length = int(self.headers.get('Content-Length', 0))
		data = self.rfile.read(length)
		print(data)
		print(self.headers)
		self.send_response(417)
	def do_POST(self):
		self.do_GET()

port = 32168

httpd = socketserver.TCPServer(("", port), handler)

httpd.serve_forever()
