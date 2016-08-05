# convert NonLinLoc control file control.html to XML
# 12Sep2000 Anthony Lomax

s|<!--Next Section-->|</NLL:program><NLL:program>|
#/<H2>/{s/.*/>/;}   # replace line 
/<H2>/d

s|<A name="|<NLL:statement><NLL:name>|g
s|"></A>|</NLL:name>|g
s|<BR><LI><B>|      <NLL:title>|g
s|</B> (<EM>|\</NLL:title> <NLL:status>|g
/<NLL:status>/s|</EM> |<comment>|;
/<NLL:status>/s|</EM>, <EM>|</NLL:status><NLL:status>|;
/<comment>/s|<EM> |</comment></NLL:status>|;
/<NLL:status>/s|<EM> |</NLL:status>|;
/<NLL:status>/s|</EM>)$|</NLL:status>|;
/<comment>/s|)$|</comment></NLL:status>|;
/<NLL:status>/s|, <EM>|</comment></NLL:status><NLL:status>|;
/<NLL:status>/s|</EM> |<comment>|;

s|<DT> <CODE><EM>|<NLL:parameters><NLL:nameset>|g
s|        <NLL:parameters>|<NLL:parameters>|g

s|</EM></CODE> (<EM>|</NLL:nameset><NLL:type>|g
s|</EM>) <DD> |</NLL:type><NLL:description>|g
s|</DL></DL>||g
/<NLL:description>/{G;s|$|</NLL:description>|;}   # append text to end of line 
/<NLL:parameters>/{G;s|$|</NLL:parameters>|;}   # append text to end of line 

s|</EM>)|</NLL:status>|g

/^<CODE>/s|<CODE>|<NLL:syntax><NLL:keyword>|g;
/^or <CODE>/s|or <CODE>|<NLL:syntax><NLL:keyword>|g;
/<NLL:syntax>/s|</CODE>|</NLL:paramset></NLL:syntax>|g;
/<NLL:syntax>/s|<BR>||g;
/<NLL:syntax>/s| <EM>|</NLL:keyword><NLL:paramset>|g;

s|<LI>|<NLL:note>|
/<NLL:note>/{G;s|$|</NLL:note>|;}   # append text to end of line 
s|        <NLL:note>|<NLL:note>|g


/<B>Notes:/d
/<P>/d
/<DD><DL>/d
/<B>Parameters:/d
/<DL>/d
/<UL>/d
/UL>/d
/A HREF="#/d
/A HREF=#/d
/<DIV/d
/<DIV/d
/<OL>/d
/OL></d

/^  <DD>/s|  <DD>|<NLL:summary>|g;
/<NLL:summary>/{G;s|$|</NLL:summary>|;}   # append text to end of line 
s|^</DL>|</NLL:statement>|;


s|<FONT COLOR=#FF0033><B>(ver 2.0)</B></FONT>|<NLL:version>2.0</NLL:version>|g

s|<NLL:description> |<NLL:description>|g

s|<NLL:syntax>|   <NLL:syntax>|g
s|<NLL:summary> |<NLL:summary>|g
s|<NLL:summary>|   <NLL:summary>|g
s|</NLL:summary>|   </NLL:summary>|g
s|<NLL:parameters>|   <NLL:parameters>|g
s|</NLL:description>|      </NLL:description>|g
s|</NLL:parameters>|   </NLL:parameters>|g
s|<NLL:note>|   <NLL:note>|g
s|</NLL:note>|   </NLL:note>|g
