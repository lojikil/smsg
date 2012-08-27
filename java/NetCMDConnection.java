import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.net.Socket;
import java.net.InetAddress;

public class NetCMDConnection {
	private Socket s = null;

	public NetCMDConnection(InetAddress host, int port) throws IOException {
		s = new Socket(host, port);
		s.setReuseAddress(true);
	}

	public NetCMDInputStream getNetCMDInputStream() throws IOException {
		return new NetCMDInputStream(s.getInputStream());
	}

	public NetCMDOutputStream getNetCMDOutputStream() throws IOException {
		return new NetCMDOutputStream(s.getOutputStream(), new NetCMD());
	}

	public void close() throws IOException {
		s.close();
	}
}

