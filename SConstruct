mtcp_root = Dir('F:\Legacy\PC\SOURCE\mTCP-src_2013-05-23')

tcp_inc_dir = mtcp_root.Dir('TCPINC')
tcp_c_dir = mtcp_root.Dir('TCPLIB')
common_inc_dir = mtcp_root.Dir('INCLUDE')
#jsmn_dir = Dir('F:\Projects\jsmn-free')
test_dir = Dir('#/TEST')

mtcp_cpp = 'packet.cpp arp.cpp eth.cpp ip.cpp tcp.cpp ' \
	'tcpsockm.cpp udp.cpp utils.cpp dns.cpp timer.cpp ipasm.asm'

#SCons doesn't detect change in H file for CPP files (and therefore doesn't
#recompile them)?... toy with nwBackupCodes enum as need what happens.
debug_mode = ARGUMENTS.get('debug', 0)
nwbackup_src = 'NWBACKUP.C DIR.C CONTROL.C BACKUP.C RESTORE.C MTCPFTP.CPP'
#jsmn_src = 'jsmn.c'

#test_src = [test_dir.File(s) for s in Split('dirtest.c')]
#print test_src
#test_env = DefaultEnvironment()

env = Environment(tools = ['watcom'], USEWASM=True)
env['MEMMODEL16'] = 'L'
env['ASFLAGS'] = '-zq -m${MEMMODEL} -0'
env.Append(CPPDEFINES = [('CFG_H', '\"nwbackup.cfg\"')]) #, 'JSMN_PARENT_LINKS'])
if not debug_mode:
  pass
  #env.Append(CPPDEFINES = ['NDEBUG'])
env.Append(CPPPATH = [Dir('#'), tcp_inc_dir]) #, jsmn_dir]) 
#compile_options = -0 $(memory_model) -DCFG_H="ftp.cfg" -oh -ok -ot -s -oa -ei -zp2 -zpw -we -ob -ol+ -oi+
#/onatx /oh /oi+ /ei /zp8 /0 /fpi87

if debug_mode:
  env.Append(CCFLAGS = '-w=2 -od -bt=dos -d2 -ecw')
else:
  env.Append(CCFLAGS = '-0 -w=2 -oh -ok -ot -s -oa -ol+ -ei -zp2 -bt=dos -ecw', CFLAGS= '-oi', CXXFLAGS = '-oi+')
env.Append(LINKFLAGS = 'system dos debug all option stack=4096')

#env.Append(LIBPATH = jsmn_dir)
#env.Append(CCFLAGS = '--pedantic')
	
#conf = Configure(env)

#env.VariantDir('#', tcp_c_dir, False) #Includes a directory, but compiles in current.
#env.VariantDir('#', jsmn_dir, False)

mtcp_objs = env.Object(env.File(Split(mtcp_cpp), tcp_c_dir))
#jsmn_lib = env.Library(env.File(Split(jsmn_src), jsmn_dir))
nwbackup_objs = env.Object(Split(nwbackup_src))
nwbackup_exe = env.Program(nwbackup_objs + mtcp_objs) #, LIBS = 'jsmn')


dir_test = env.Program([test_dir.File('dirtest.c'), 'dir.c'])
mdir_test = env.Program(test_dir.File('mdirtest.c'), CFLAGS=['-Za99'])
ftp_test = env.Program([test_dir.File('ftptest.c'), 'mtcpftp.cpp'] + mtcp_objs)
temp_test = env.Program(test_dir.File('tmptest.c'))
#ser_obj = env.Object([test_dir.File('sertest.c'), 'control.c', 'dir.c'])
#ser_test = env.Program(ser_obj, LIBS = 'jsmn')
#ftpret_test = env.Program([test_dir.File('ftpret.c'), 'mtcpftp.cpp'] + mtcp_objs)

test_exes = [dir_test, mdir_test, ftp_test, temp_test] #, ser_test]
test_alias = env.Alias('test', test_exes)
all_alias = env.Alias('all', [nwbackup_exe, test_exes])

def_target = mtcp_objs + nwbackup_exe
Default(def_target)
#Default(mtcp_objs + nwbackup_objs)
for tgt in [def_target, test_alias, all_alias]:
  Clean(tgt, [Glob('*.map'), Glob('*.err')])
  #Clean(tgt, [Glob('*.orig'), Glob('*.map'), Glob('*.err')])
