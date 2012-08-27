/**
 * Contains a netcommand. Provides functions to construct, compile and decompile netcommands.
 *
 * netcommand (in network order) :: [ncmd](word) [numargs](word) {[arglen](dword) [argdata](arglen)}(* numargs)
 *
 * @version 0.0.1
 * @author Wonderkat
 */
import java.util.*;
//import java.lang.reflect.*;

public interface NetCMDInterface {
//	private int cmd = -1;
//	private List args = null;

//	public NetCMDInterface();
//	public NetCMDInterface(int cmd);
//	public NetCMDInterface(int cmd, Set args);

	public void addArgumentByte(byte arg);
	public void addArgumentWord(int arg);
	public void addArgumentDWord(int arg);
	public void addArgumentString(String arg);

	public byte getArgumentByte(int idx);
	public int getArgumentWord(int idx) throws NetCmdException;
	public int getArgumentDWord(int idx) throws NetCmdException;
	public String getArgumentString(int idx);

	public byte[] constructNetCMD(NetCMD cmd);
	public NetCMD dissectNetCMD(byte[] data);
}

