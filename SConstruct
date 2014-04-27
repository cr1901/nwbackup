mtcp_root = Dir('F:\Legacy\PC\SOURCE\mTCP-src_2013-05-23')

tcp_inc_dir = mtcp_root.Dir('TCPINC')
tcp_c_dir = mtcp_root.Dir('TCPLIB')
common_inc_dir = mtcp_root.Dir('INCLUDE')
#jsmn_dir = Dir('#/jsmn')
test_dir = Dir('#/TEST')

mtcp_cpp = 'packet.cpp arp.cpp eth.cpp ip.cpp tcp.cpp ' \
	'tcpsockm.cpp udp.cpp utils.cpp dns.cpp timer.cpp ipasm.asm'

#nwbackup_src = 'nwbackup.cpp msbackup.c'
nwbackup_src = 'NWBACKUP.C DIR.C MTCPFTP.CPP'

#test_src = [test_dir.File(s) for s in Split('dirtest.c')]
#print test_src
#test_env = DefaultEnvironment()

env = Environment(tools = ['watcom'], USEWASM=True)
env['MEMMODEL16'] = 'L'
env['ASFLAGS'] = '-zq -m${MEMMODEL} -0'
env.Append(CPPDEFINES = [('CFG_H', '\"nwbackup.cfg\"'), 'NDEBUG'])
#env.Append(CPPDEFINES = [('CFG_H', '\"nwbackup.cfg\"')])
env.Append(CPPPATH = [Dir('#'), tcp_inc_dir]) 
#compile_options = -0 $(memory_model) -DCFG_H="ftp.cfg" -oh -ok -ot -s -oa -ei -zp2 -zpw -we -ob -ol+ -oi+
#/onatx /oh /oi+ /ei /zp8 /0 /fpi87

env.Append(CCFLAGS = '-w=2 -od -bt=dos -d2 -ecw')
#env.Append(CCFLAGS = '-0 -w=2 -oh -ok -ot -s -oa -ol+ -ei -zp2 -bt=dos -ecw', CFLAGS= '-oi', CXXFLAGS = '-oi+')
env.Append(LINKFLAGS = 'system dos debug all option stack=4096')
	
#env.Append(CCFLAGS = '--pedantic')
	
#conf = Configure(env)

env.VariantDir('.', tcp_c_dir, False) #Includes a directory, but compiles in current.
mtcp_objs = env.Object(Split(mtcp_cpp))
nwbackup_objs = env.Object(Split(nwbackup_src))
nwbackup_exe = env.Program(nwbackup_objs + mtcp_objs)


dir_test = env.Program([test_dir.File('dirtest.c'), 'dir.c'])
mdir_test = env.Program(test_dir.File('mdirtest.c'), CFLAGS=['-Za99'])
ftp_test = env.Program([test_dir.File('ftptest.c'), 'mtcpftp.cpp'] + mtcp_objs)
temp_test = env.Program(test_dir.File('tmptest.c'))
#ftpret_test = env.Program([test_dir.File('ftpret.c'), 'mtcpftp.cpp'] + mtcp_objs)

test_exes = [dir_test, mdir_test, ftp_test, temp_test]
env.Alias('test', test_exes)

Default(mtcp_objs + nwbackup_exe)
#Default(mtcp_objs + nwbackup_objs)
