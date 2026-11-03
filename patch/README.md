# sqlite补丁应用与创建流程

为了解决sqlite patch数量随着修改与提交逐渐增多以及sqlite patch生成流程较为繁琐的问题，在此重构sqlite补丁模块。后续开发者代码修改与提交可参考以下流程进行：

1. 目前所有patch已被整合成一个文件。patch路径为"patch/all_sqlite.patch"，该文件在本文后续会被简称为“patch文件”。

2. 目前完整版sqlite源代码（已应用所有补丁）会被追踪，完整版源代码路径为"patch/patched_sqlite"。该目录在本文后续会被简称为“完整代码目录”。开发者可直接对完整代码目录下的源代码进行开发。

3. 在完整代码目录下开发完毕后，开发者可以执行"patch/sqlite_patch_generator.sh"脚本，该脚本会基于原始sqlite代码，与开发者当前完整代码目录下的文件进行递归比对，生成最新版的patch文件，并将旧版的patch文件替换掉。当该脚本打印"sqlite_patch_generator.sh completed"时，意味着执行成功，有效的patch文件被生成，除此之外意味着执行失败，patch不可用，请解决相应问题后重新执行该脚本。该脚本并非生成patch的唯一方式。若脚本不可用，开发者可按重构前的方式手动生成patch。

4. 开发者在提交时，应追踪新生成的patch文件以及完整代码目录的修改。

5. 当开发者编译sqlite模块前，应确保patch文件与当前完整代码目录相契合（也就是原始sqlite代码应用了patch文件后，应与当前的完整代码目录中文件完全一致），否则编译会被中止。
