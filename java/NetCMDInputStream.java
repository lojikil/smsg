import java.io.InputStream;
import java.io.IOException;

public class NetCMDInputStream extends InputStream {
	private InputStream is = null;
	private String incompleteBuffer = null;	//buffer to hold partial netcommands (in case they are fragmented over multiple TCP packets)

	public NetCMDInputStream(InputStream is) {
		this.is = is;
	}

	public boolean markSupported() {
		return false;
	}

	/**
	 * Returns the number of complete netcommands available.
	 *
	 * @return int the amount of available netcommands
	 */
	public int available() {
		return 0;
	}

	/**
	 * This method must be here to override abstract int read() in InputStream.
	 * It will, however, always throw an IOException.
	 */
	public int read() throws IOException {
		throw new IOException("byte reading not supported");
		//return -1;
	}

	public NetCMD readNetCMD() {
		return null;
	}
}

