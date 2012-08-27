import java.io.OutputStream;
import java.io.IOException;

public class NetCMDOutputStream extends OutputStream {
	private OutputStream os = null;
	private NetCMDInterface ncmdImpl = null;

	public NetCMDOutputStream(OutputStream os, NetCMDInterface ncmdImpl) {
		this.os = os;
		this.ncmdImpl = ncmdImpl;
	}

	/**
	 * This method must be here to override abstract void write(int b) in OutputStream.
	 * It will, however, always throw an IOException.
	 */
	public void write(int b) throws IOException {
		throw new IOException("byte writing not supported");
	}

	public void writeNetCMD(NetCMD ncmd) {
		byte[] data = ncmdImpl.constructNetCMD(ncmd);
	}
}

