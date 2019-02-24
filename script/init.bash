cat << SQL | mysql
install plugin mrbdb soname 'ha_mrbdb.so' ;
create database hello ;
create table hello.mruby (id text) engine = mrbdb ;
SQL
