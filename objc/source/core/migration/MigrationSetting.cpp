/*
 * Tencent is pleased to support the open source community by making
 * WCDB available.
 *
 * Copyright (C) 2017 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *       https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <WCDB/Core.h>

namespace WCDB {

#pragma mark - Initialize
std::shared_ptr<MigrationSetting> MigrationSetting::setting(
    const std::list<std::shared_ptr<MigrationInfo>> &infos)
{
    return std::shared_ptr<MigrationSetting>(new MigrationSetting(infos));
}

MigrationSetting::MigrationSetting(
    const std::list<std::shared_ptr<MigrationInfo>> &infos)
    : m_started(false)
    , m_onMigrated(nullptr)
    , m_rowPerStep(10)
    , m_onConflict(nullptr)
#ifdef DEBUG
    , hash(hashedInfos(infos))
#endif
{
    WCTAssert(!infos.empty(), "Migration infos can't be empty.");
    for (const auto &info : infos) {
        if (!info->isSameDatabaseMigration()) {
            auto iter = m_schemas.find(info->schema);
            if (iter == m_schemas.end()) {
                iter =
                    m_schemas
                        .insert({info->schema, {info->sourceDatabasePath, 0}})
                        .first;
            }
            ++iter->second.second;
        }
        WCTAssert(
            m_infos.find(info->targetTable) == m_infos.end(),
            "Migrating multiple tables to the same table is not allowed.");
        m_infos.insert({info->targetTable, info});
    }
}

#ifdef DEBUG
int64_t MigrationSetting::hashedInfos(
    const std::list<std::shared_ptr<MigrationInfo>> &infos) const
{
    std::string hashSource;
    for (const auto &info : infos) {
        hashSource.append(info->targetTable);
        hashSource.append(info->sourceTable);
        hashSource.append(info->sourceDatabasePath);
    }
    return std::hash<std::string>{}(hashSource);
}
#endif

#pragma mark - Basic
bool MigrationSetting::isSameDatabaseMigration() const
{
    SharedLockGuard lockGuard(m_lock);
    return m_schemas.empty();
}

const std::map<std::string, std::pair<std::string, int>> &
MigrationSetting::getSchemasForAttaching() const
{
//TODO refactor
#ifdef DEBUG
    WCTInnerAssert(m_lock.debug_isSharedLocked());
#endif
    return m_schemas;
}

const std::map<std::string, std::shared_ptr<MigrationInfo>> &
MigrationSetting::getInfos() const
{
//TODO refactor
#ifdef DEBUG
    WCTInnerAssert(m_lock.debug_isSharedLocked());
#endif
    return m_infos;
}

bool MigrationSetting::isStarted() const
{
    return m_started.load();
}

bool MigrationSetting::isMigrated() const
{
    SharedLockGuard lockGuard(m_lock);
    return m_infos.empty();
}

void MigrationSetting::markAsStarted()
{
    LockGuard lockGuard(m_lock);
    m_started.store(true);
}

bool MigrationSetting::markAsMigrated(const std::string &table)
{
    LockGuard lockGuard(m_lock);
    auto iter = m_infos.find(table);
    WCTInnerAssert(iter != m_infos.end());
    bool schemasChanged = false;
    if (!iter->second->isSameDatabaseMigration()) {
        auto schemaIter = m_schemas.find(iter->second->schema);
        WCTInnerAssert(schemaIter != m_schemas.end());
        if (--schemaIter->second.second == 0) {
            m_schemas.erase(schemaIter);
            schemasChanged = true;
        }
    }
    std::shared_ptr<MigrationInfo> info = iter->second;
    m_infos.erase(iter);
    if (m_onMigrated) {
        m_onMigrated(info.get());
        if (m_infos.empty()) {
            //done
            m_onMigrated(nullptr);
        }
    }
    return schemasChanged;
}

const std::shared_ptr<MigrationInfo> &
MigrationSetting::pickUpForMigration() const
{
#ifdef DEBUG
    WCTInnerAssert(m_lock.debug_isSharedLocked());
#endif
    WCTInnerAssert(!m_infos.empty());
    return m_infos.begin()->second;
}

void MigrationSetting::setMigratedCallback(const MigratedCallback &onMigrated)
{
    LockGuard lockGuard(m_lock);
    m_onMigrated = onMigrated;
}

SharedLock &MigrationSetting::getSharedLock()
{
    return m_lock;
}

void MigrationSetting::setMigrateRowPerStep(int row)
{
    LockGuard lockGuard(m_lock);
    m_rowPerStep = row;
}

int MigrationSetting::getMigrationRowPerStep() const
{
    SharedLockGuard lockGuard(m_lock);
    return m_rowPerStep;
}

void MigrationSetting::setConflictCallback(const ConflictCallback &callback)
{
    LockGuard lockGuard(m_lock);
    m_onConflict = callback;
}

bool MigrationSetting::invokeConflictCallback(const MigrationInfo *info,
                                              const long long &rowid) const
{
    SharedLockGuard lockGuard(m_lock);
    return m_onConflict ? m_onConflict(info, rowid) : false;
}

} //namespace WCDB