/*
    Copyright (C) 2023 Bernd Herzog
    Copyright (C) 2024 Mark Thompson

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

MEMORY
{
    /*
     * External apps: regions can't overlap so addresses are corrected after build.
     * Picking uncommon address values for search & replace in binaries (no false positives) - 0xADB00000-0xADEF0000 seems to be good.
     * Also need to consider processor memory map - reading 0xADxxxxxx generates a fault which may be better than unexpected behavior.
     * External app address ranges below must match those in python file "external_app_info.py".
     */
    ram_external_app_ext_scanner(rwx) : org = 0xADB10000, len = 32k
}

SECTIONS
{
    .external_app_ext_scanner : ALIGN(4) SUBALIGN(4)
    {
        KEEP(*(.external_app.app_ext_scanner.application_information));
        *(*ui*external_app*ext_scanner*);
    } > ram_external_app_ext_scanner
}
