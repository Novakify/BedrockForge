import dev.lyricsflow.lyrics.*;
import dev.lyricsflow.media.TitleCleaner;
import dev.lyricsflow.util.Anim;
import java.nio.file.*;
import java.util.*;

public class Golden {
  static String q(String s){ StringBuilder b=new StringBuilder("\""); for(char c: s.toCharArray()){ if(c=='"'||c=='\\') b.append('\\').append(c); else if(c<0x20) b.append(String.format("\\u%04x",(int)c)); else b.append(c);} return b.append('"').toString(); }
  static String lyr(Lyrics l){
    if(l==null) return "null";
    StringBuilder b=new StringBuilder("{\"synced\":"+l.synced+",\"wordSynced\":"+l.wordSynced+",\"source\":"+q(l.source)+",\"lines\":[");
    for(int i=0;i<l.lines.size();i++){ Lyrics.Line ln=l.lines.get(i); if(i>0)b.append(',');
      b.append("{\"text\":"+q(ln.text)+",\"start\":"+ln.start+",\"end\":"+ln.end+",\"estimated\":"+ln.estimated+",\"words\":[");
      for(int j=0;j<ln.words.size();j++){ Lyrics.Word w=ln.words.get(j); if(j>0)b.append(','); b.append("["+q(w.text)+","+w.start+","+w.end+"]"); }
      b.append("]}"); }
    return b.append("]}").toString();
  }
  public static void main(String[] a) throws Exception {
    Path d=Paths.get(a[0]);
    String lrc=Files.readString(d.resolve("song.lrc")), enh=Files.readString(d.resolve("enhanced.lrc"));
    StringBuilder y=new StringBuilder(Files.readString(d.resolve("netease_yrc.txt")));
    StringBuilder o=new StringBuilder("{\n");
    o.append("\"lrc\":"+lyr(FetchLite.parseLrc(lrc,200000L))+",\n");
    o.append("\"lrc_nodur\":"+lyr(FetchLite.parseLrc(lrc,0L))+",\n");
    o.append("\"enhanced\":"+lyr(FetchLite.parseLrc(enh,60000L))+",\n");
    o.append("\"yrc\":"+lyr(FetchLite.parseYrc(y.toString(),200000L))+",\n");
    o.append("\"spread\":"+lyr(FetchLite.spread("Line one here\n[bracket skip]\nSecond line of text\nThird and final line",180000L,"lrclib"))+",\n");
    String[] titles={"Song Name (Official Music Video) [HD]","Track - Remastered 2011","My Song (feat. Someone Else)","Beyoncé – Halo (Lyrics)","Song (sped up + reverb)","Ćwiczenie【MV】","Title (Live) - "};
    o.append("\"clean\":{"); for(int i=0;i<titles.length;i++){ if(i>0)o.append(','); o.append(q(titles[i])+":"+q(TitleCleaner.clean(titles[i]))); } o.append("},\n");
    String[] artists={"Artist A, Artist B","A & B","Daft Punk x Pharrell","X feat. Y","Single","AC/DC","Simon & Garfunkel"};
    o.append("\"mainArtist\":{"); for(int i=0;i<artists.length;i++){ if(i>0)o.append(','); o.append(q(artists[i])+":"+q(TitleCleaner.mainArtist(artists[i]))); } o.append("},\n");
    String[] folds={"Beyoncé","İstanbul ıi","Ñandú Café","ŁÓDŹ","Ελληνικά","Привет"};
    o.append("\"fold\":{"); for(int i=0;i<folds.length;i++){ if(i>0)o.append(','); o.append(q(folds[i])+":"+q(TitleCleaner.fold(folds[i]))); } o.append("},\n");
    String[][] yt={{"(3) Artist - Song (Official Video) - YouTube","ChanVEVO"},{"Song Title | Live Session","Channel - Topic"},{"Just A Title","Some Official"}};
    o.append("\"splitYoutube\":["); for(int i=0;i<yt.length;i++){ if(i>0)o.append(','); String[] r=TitleCleaner.splitYoutube(yt[i][0],yt[i][1]); o.append("["+q(yt[i][0])+","+q(yt[i][1])+","+q(r[0])+","+q(r[1])+"]"); } o.append("],\n");
    String[][] vs={{"Artist A & Artist B","Song (feat. C) [Official Video]"},{"Beyoncé","Crème Brûlée - Radio Edit"}};
    o.append("\"variants\":["); for(int i=0;i<vs.length;i++){ if(i>0)o.append(','); o.append("{\"in\":["+q(vs[i][0])+","+q(vs[i][1])+"],\"out\":["); List<String[]> r=TitleCleaner.variants(vs[i][0],vs[i][1]); for(int j=0;j<r.size();j++){ if(j>0)o.append(','); o.append("["+q(r.get(j)[0])+","+q(r.get(j)[1])+"]"); } o.append("]}"); } o.append("],\n");
    long[] seeds={0,1,42,-7,7919L*3+104729L*2,123456789012345L,-1};
    o.append("\"hash01\":["); for(int i=0;i<seeds.length;i++){ if(i>0)o.append(','); o.append("["+seeds[i]+","+Anim.hash01(seeds[i])+"]"); } o.append("],\n");
    float[] ts={0f,0.1f,0.5f,0.9f,1f};
    o.append("\"ease\":["); for(int i=0;i<ts.length;i++){ if(i>0)o.append(','); o.append("["+ts[i]+","+Anim.easeOutBack(ts[i])+","+Anim.easeInOut(ts[i])+","+Anim.easeOutCubic(ts[i])+"]"); } o.append("]\n}\n");
    System.out.print(o);
  }
}
