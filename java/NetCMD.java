/**
 * Contains a netcommand. Provides functions to construct, compile and decompile netcommands.
 *
 * netcommand (in network order) :: [ncmd](word) [numargs](word) {[arglen](dword) [argdata](arglen)}(* numargs)
 *
 * @version 0.0.1
 * @author Wonderkat
 */
import java.util.*;
import java.lang.IllegalArgumentException;
//import java.lang.reflect.*;

public class NetCMD implements NetCMDInterface {
	private int cmd;
	private List args = new LinkedList();

	public NetCMD() {
		this(-1, null);
//convertNum(1, 2);
//System.out.println("---");
//convertNum(0xf080, 2);
	}

	public NetCMD(int cmd) {
		this(cmd, null);
	}

	public NetCMD(int cmd, Set args) {
		this.cmd = cmd;
		this.args = new LinkedList(args);
	}


	public void addArgumentByte(byte arg) {byte[] argarray = {arg}; args.add(new String(argarray));}
	public void addArgumentWord(int arg) {args.add(new String(toByteArray(arg, 2)));}
	public void addArgumentDWord(int arg) {args.add(new String(toByteArray(arg, 4)));}
	public void addArgumentString(String arg) {args.add(arg);}

	public byte getArgumentByte(int idx) {
		return (byte)((String)args.get(idx)).charAt(0);
	}

	public int getArgumentWord(int idx) throws NetCmdException {
		String t = (String)args.get(idx);
		if(t.length() == 2) {
			return toInt(t.getBytes());
		} else {
			throw new NetCmdException("can't convert " + t.length() + " bytes to word");
		}
	}

	public int getArgumentDWord(int idx) throws NetCmdException {
		String t = (String)args.get(idx);
		if(t.length() == 4) {
			return toInt(t.getBytes());
		} else {
			throw new NetCmdException("can't convert " + t.length() + " bytes to dword");
		}
	}

	public String getArgumentString(int idx) {
		String t = (String)args.get(idx);
		int o;
		if((o = t.indexOf(0)) > -1) t = t.substring(0, o);	//treat as ASCIIZ string
		return t;
	}

	public byte[] constructNetCMD(NetCMD cmd) {
		if(cmd == null) throw IllegalArgumentException("no netcommand to construct");
		byte[] data = new byte[2];
		//String allargs = new String();
		//Iterator it = args.iterator();
		//while(it.hasNext()) {
		//	String ta = (String)it.next();
		//	byte[] talen = toByteArray(ta.length(), 4);
		//	String targ = new String(talen).concat(ta);
		//	allargs.concat(targ);
		//}
		//byte[] ncn = toByteArray(cmd, 2);
		//byte[] numargs = toByteArray(args.size(), 2);
		//String total = new String(ncn);
		//total.concat(new String(numargs));
		//total.concat(allargs);

		return data;
	}

	public NetCMD dissectNetCMD(byte[] data) {
		NetCMD ncmd = new NetCMD();
		return ncmd;
	}


	//NOTE: should only accept 2 or 4 (& possibly 8) bytes of data
	private int toInt(byte[] array) {
		int r = 0;
		return r;
	}

	//NOTE: does this work? iirc there is a java function for this (hton, ntoh, etc.)
	private byte[] toByteArray(int n, int bytes) {
		byte [] r = new byte[bytes];
//System.out.println("converting: " + n + "(" + bytes + ")");
		for(int i = bytes - 1; i >= 0; i--) {
			r[bytes - 1 - i] += new Integer((n & 0xFF << (8 * i)) >> (8 * i)).byteValue();
//System.out.println("r[" + (bytes - 1 - i) + "] = " + new Integer((n & 0xFF << (8 * i)) >> (8 * i)).byteValue());
		}
		return r;
	}
}

