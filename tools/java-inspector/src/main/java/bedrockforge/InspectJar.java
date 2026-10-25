package bedrockforge;

import org.objectweb.asm.*;
import net.fabricmc.mappingio.MappingReader;
import net.fabricmc.mappingio.tree.MemoryMappingTree;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/** Read-only Java bytecode dependency/method inventory. Never loads classes. */
public final class InspectJar {
    private static String esc(String s) {
        return s.replace("\\","\\\\").replace("\"","\\\"").replace("\n","\\n").replace("\r","\\r");
    }
    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.err.println("Usage: java-inspector <mod.jar> [mappings.tiny]");
            System.exit(2);
        }
        Path path=Paths.get(args[0]);
        if (!Files.isRegularFile(path)) throw new IOException("Missing JAR: " + path);
        MemoryMappingTree mappings=null;
        if (args.length>1) {
            mappings=new MemoryMappingTree();
            MappingReader.read(Paths.get(args[1]),mappings);
        }
        SortedMap<String,Integer> refs=new TreeMap<>();
        int[] classes={0},methods={0},fields={0};
        ArrayList<String> geckoClasses=new ArrayList<>();
        try (ZipFile zip=new ZipFile(path.toFile())) {
            Enumeration<? extends ZipEntry> entries=zip.entries();
            while(entries.hasMoreElements()){
                ZipEntry entry=entries.nextElement();
                if (!entry.getName().endsWith(".class")) continue;
                byte[] bytes;
                try(InputStream input=zip.getInputStream(entry)){bytes=input.readAllBytes();}
                // Skip malformed entries with a clear diagnostic rather than executing anything.
                ClassReader reader=new ClassReader(bytes);
                final String className=reader.getClassName();
                classes[0]++;
                reader.accept(new ClassVisitor(Opcodes.ASM9){
                    private void ref(String name){
                        if(name==null) return;
                        if(name.startsWith("software/bernie/geckolib/") || name.startsWith("com/geckolib/")){
                            refs.merge(name,1,Integer::sum);
                            if (!geckoClasses.contains(className)) geckoClasses.add(className);
                        }
                    }
                    @Override public void visit(int version,int access,String name,String sig,String parent,String[] interfaces){
                        ref(parent); if(interfaces!=null) for(String iface:interfaces) ref(iface);
                    }
                    @Override public FieldVisitor visitField(int access,String name,String descriptor,String sig,Object value){
                        fields[0]++; ref(Type.getType(descriptor).getInternalName());
                        return null;
                    }
                    @Override public MethodVisitor visitMethod(int access,String name,String descriptor,String sig,String[] exceptions){
                        methods[0]++; 
                        return new MethodVisitor(Opcodes.ASM9){
                            @Override public void visitMethodInsn(int opcode,String owner,String method,String desc,boolean itf){ref(owner);}
                            @Override public void visitFieldInsn(int opcode,String owner,String field,String desc){ref(owner);}
                            @Override public void visitTypeInsn(int opcode,String type){ref(type);}
                            @Override public void visitLdcInsn(Object value){if(value instanceof Type t) ref(t.getInternalName());}
                        };
                    }
                },ClassReader.SKIP_DEBUG|ClassReader.SKIP_FRAMES);
            }
        }
        Collections.sort(geckoClasses);
        StringBuilder output=new StringBuilder();
        output.append("{\n  \"jar\": \"").append(esc(path.getFileName().toString())).append("\",");
        output.append("\n  \"classes\": ").append(classes[0]).append(", \"methods\": ").append(methods[0]);
        output.append(", \"fields\": ").append(fields[0]);
        output.append(",\n  \"mapping_namespace_count\": ").append(mappings==null?"null":mappings.getDstNamespaces().size()+1);
        output.append(",\n  \"geckolib_referencing_classes\": [");
        for(int i=0;i<geckoClasses.size();i++){if(i>0)output.append(",");output.append("\n    \"").append(esc(geckoClasses.get(i))).append("\"");}
        output.append("\n  ],\n  \"geckolib_api_references\": {");
        int i=0;
        for(var e:refs.entrySet()){
            if(i++>0)output.append(",");
            output.append("\n    \"").append(esc(e.getKey())).append("\": ").append(e.getValue());
        }
        output.append("\n  },\n  \"verification\": \"Static ASM visitor only; no mod classes executed\"\n}");
        System.out.println(output);
    }
}
